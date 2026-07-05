/******************************************************************************
 * Copyright (c) 2006-2012 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import "PortChecker.h"

#import <netdb.h>
#import <arpa/inet.h>
#import <Security/Security.h>

#define CHECKER_HOST @"portcheck.transmissionbt.com"
#define CHECK_FIRE 3.0

@interface PortChecker ()
@property (nonatomic, assign) BOOL isCancelled;
@end

@interface PortChecker (Private)
- (void) startProbe: (NSTimer *) timer;
- (void) callBackWithStatus: (port_status_t) status;
@end


@implementation PortChecker

- (id) initForPort: (NSInteger) portNumber delay: (BOOL) delay withDelegate: (id) delegate
{
    if ((self = [super init]))
    {
        fDelegate = delegate;
        fStatus = PORT_STATUS_CHECKING;
        self.isCancelled = NO;

        fTimer = [NSTimer scheduledTimerWithTimeInterval: CHECK_FIRE target: self selector: @selector(startProbe:) userInfo: @(portNumber) repeats: NO];
        if (!delay)
            [fTimer fire];
    }
    return self;
}

- (void) dealloc
{
    [fTimer invalidate];
}

- (port_status_t) status
{
    return fStatus;
}

- (void) cancelProbe
{
    self.isCancelled = YES;

    [fTimer invalidate];
    fTimer = nil;

    [fConnection cancel];
}

- (void) connection: (NSURLConnection *) connection didReceiveResponse: (NSURLResponse *) response
{
    [fPortProbeData setLength: 0];
}

- (void) connection: (NSURLConnection *) connection didReceiveData: (NSData *) data
{
    [fPortProbeData appendData: data];
}

- (void) connection: (NSURLConnection *) connection didFailWithError: (NSError *) error
{
    NSLog(@"Unable to get port status: connection failed (%@)", [error localizedDescription]);
    [self callBackWithStatus: PORT_STATUS_ERROR];
}

- (void) connectionDidFinishLoading: (NSURLConnection *) connection
{
    NSString * probeString = [[NSString alloc] initWithData: fPortProbeData encoding: NSUTF8StringEncoding];

    fPortProbeData = nil;

    if (probeString)
    {
        if ([probeString isEqualToString: @"1"])
            [self callBackWithStatus: PORT_STATUS_OPEN];
        else if ([probeString isEqualToString: @"0"])
            [self callBackWithStatus: PORT_STATUS_CLOSED];
        else
        {
            NSLog(@"Unable to get port status: invalid response (%@)", probeString);
            [self callBackWithStatus: PORT_STATUS_ERROR];
        }
    }
    else
    {
        NSLog(@"Unable to get port status: invalid data received");
        [self callBackWithStatus: PORT_STATUS_ERROR];
    }
}

/**
 Override SSL certificate validation to check against domain instead of IP.
*/
- (void) connection: (NSURLConnection *) connection willSendRequestForAuthenticationChallenge: (NSURLAuthenticationChallenge *) challenge
{
    if (![challenge.protectionSpace.authenticationMethod isEqualToString:NSURLAuthenticationMethodServerTrust])
    {
        [challenge.sender performDefaultHandlingForAuthenticationChallenge:challenge];
        return;
    }

    SecTrustRef serverTrust = challenge.protectionSpace.serverTrust;

    // Force the evaluation to validate against the domain rather than the literal IP
    SecPolicyRef policy = SecPolicyCreateSSL(true, (__bridge CFStringRef)CHECKER_HOST);
    SecTrustSetPolicies(serverTrust, policy);

    BOOL trustIsValid = NO;

    SecTrustResultType result;
    if (SecTrustEvaluate(serverTrust, &result) == errSecSuccess) {
        trustIsValid = (result == kSecTrustResultProceed || result == kSecTrustResultUnspecified);
    }

    CFRelease(policy);

    if (trustIsValid) {
        [challenge.sender useCredential:[NSURLCredential credentialForTrust:serverTrust] forAuthenticationChallenge:challenge];
    } else {
        NSLog(@"Unable to get port status: SSL certificate validation failed");
        [challenge.sender cancelAuthenticationChallenge:challenge];
    }
}

@end


@implementation PortChecker (Private)

- (void) startProbe: (NSTimer *) timer
{
    fTimer = nil;
    NSInteger port = [[timer userInfo] integerValue];

    __weak typeof(self) weakSelf = self;

    // Resolve host to IPv4 asynchronously on a GCD background queue
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        struct addrinfo hints;
        struct addrinfo *res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        NSString *ipAddress = nil;

        if (getaddrinfo([CHECKER_HOST UTF8String], NULL, &hints, &res) == 0) {
            for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
                if (p->ai_family == AF_INET) {
                    char ipstr[INET_ADDRSTRLEN];
                    if (inet_ntop(AF_INET, &((struct sockaddr_in *)p->ai_addr)->sin_addr, ipstr, sizeof(ipstr))) {
                        ipAddress = [NSString stringWithUTF8String:ipstr];
                        break;
                    }
                }
            }
            freeaddrinfo(res);
        }

        // Dispatch back to the main thread to construct and fire the URL request
        dispatch_async(dispatch_get_main_queue(), ^{
            typeof(self) strongSelf = weakSelf;
            // Exit safely if deallocated or explicitly cancelled during the background DNS lookup
            if (!strongSelf || strongSelf.isCancelled) {
                return;
            }
            if (!ipAddress) {
                NSLog(@"Unable to get port status: failed to resolve hostname");
                [strongSelf callBackWithStatus: PORT_STATUS_ERROR];
                return;
            }

            NSString *urlString;
             // Build the URL using the resolved raw IPv4 address
            urlString = [NSString stringWithFormat:@"https://%@/%ld", ipAddress, (long)port];

            NSMutableURLRequest * portProbeRequest = [NSMutableURLRequest requestWithURL: [NSURL URLWithString: urlString]
                                                cachePolicy: NSURLRequestReloadIgnoringLocalCacheData timeoutInterval: 15.0];
            [portProbeRequest setValue:@"no-cache" forHTTPHeaderField:@"Cache-Control"];
            [portProbeRequest setValue:CHECKER_HOST forHTTPHeaderField:@"Host"];

            if ((strongSelf->fConnection = [[NSURLConnection alloc] initWithRequest: portProbeRequest delegate: strongSelf])) {
                strongSelf->fPortProbeData = [[NSMutableData alloc] init];
            } else {
                NSLog(@"Unable to get port status: failed to initiate connection");
                [strongSelf callBackWithStatus: PORT_STATUS_ERROR];
            }
        });
    });
}

- (void) callBackWithStatus: (port_status_t) status
{
    fStatus = status;

    if (fDelegate && [fDelegate respondsToSelector: @selector(portCheckerDidFinishProbing:)])
        [fDelegate performSelectorOnMainThread: @selector(portCheckerDidFinishProbing:) withObject: self waitUntilDone: NO];
}

@end
