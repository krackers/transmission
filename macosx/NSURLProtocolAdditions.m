//
//  NSURLProtocolAdditions.m
//  Transmission
//
//  Copyright (c) 2024 The Transmission Project. All rights reserved.
//

#import "NSURLProtocolAdditions.h"


@implementation IPv4OnlyHTTPProtocol {
	CFReadStreamRef readStream;
	CFWriteStreamRef writeStream;
	BOOL stopped;
}

+ (void) load {
	[NSURLProtocol registerClass: [IPv4OnlyHTTPProtocol class]];
}

+ (BOOL)canInitWithRequest:(NSURLRequest *)request
{
	NSString *scheme = request.URL.scheme;
	return [scheme caseInsensitiveCompare:@"http4"] == NSOrderedSame ||
	[scheme caseInsensitiveCompare:@"https4"] == NSOrderedSame;
}

+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request
{
	return request;
}

- (BOOL) isTLS
{
	return [[self request].URL.scheme caseInsensitiveCompare:@"https4"] == NSOrderedSame;
}

- (void)stopLoading
{
	stopped = YES;
	if (readStream) {
		CFReadStreamClose(readStream);
		CFRelease(readStream);
		readStream = nil;
	}
	if (writeStream) {
		CFWriteStreamClose(writeStream);
		CFRelease(writeStream);
		writeStream = nil;
	}
}

/**
 Currently we synchronously do the address resolution, socket connect, and write
 (the read is async, mainly because it gives us a nice callback for when stream is closed).
 
 This could technically be made async at the cost of a ton more code. We just use a GCD queue
 for the synchronous part to avoid too much complexity.
 */
- (void)startLoading
{
	// CFRunLoop is thread-safe. Retain the underlying loop
	// so we can safely queue blocks to it (even if the backing thread is gone,
	// i.e. runloop is never serviced, we can still safely add things to it).
	CFRunLoopRef connectionThread = CFRunLoopGetCurrent();
	CFRetain(connectionThread);
	
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0), ^{
		int fdOut;
		NSError *error = [self connectIPv4Socket: &fdOut];
		// Optimization: if stopped is signalled here itself we can avoid triggering
		// a runloop run entirely.
		if (error || self->stopped) {
			CFRelease(connectionThread);
			if (!self->stopped)
				[self.client URLProtocol:self didFailWithError:error];
			return;
		}
		CFRunLoopPerformBlock(connectionThread, kCFRunLoopCommonModes, ^{
			// This check is absolutely necessary, since it's the only
			// way we have a guarnateed happens-before relation, as
			// self->stopped will only be signalled from the runloop thread
			// so it's proper value is visible here.
			// Additionally, "self" has only been retained within the scope
			// of this block. Once we set up the async callbacks we are no longer
			// guaranteed self is valid.
			if (self->stopped) {
				return;
			}
			NSError *error = [self setupStreamForSocket: fdOut];
			if (error) {
				[self.client URLProtocol:self didFailWithError:error];
				return;
			}
			
			[self buildAndSendHTTPRequestFromRequest];
			[self setupReadStreamCallbacksForRunLoop: connectionThread];
		});
		CFRunLoopWakeUp(connectionThread);
		// All done manipulating the runloop now
		CFRelease(connectionThread);
	});
	
}


- (void)buildAndSendHTTPRequestFromRequest {
	NSMutableString *httpRequest = [NSMutableString string];
	NSURLRequest *request = self.request;
	
	// 1. HTTP Method
	NSString *httpMethod = request.HTTPMethod ?: @"GET"; // Default to GET if nil
	
	// 2. Path (includes query string if any)
	NSString *path = [request.URL.path isEqualToString:@""] ? @"/" : request.URL.path;  // Default to root path if empty
	if (request.URL.query) {
		path = [path stringByAppendingFormat:@"?%@", request.URL.query];
	}
	
	// 3. HTTP Version. Use 1.0 to avoid parsing chunked.
	[httpRequest appendFormat:@"%@ %@ HTTP/1.0\r\n", httpMethod, path];
	
	// 4. Host Header
	NSString *hostHeader = request.URL.host;
	if (request.URL.port) {
		hostHeader = [hostHeader stringByAppendingFormat:@":%@", request.URL.port];
	}
	[httpRequest appendFormat:@"Host: %@\r\n", hostHeader];
	
	// 5. Headers from the NSURLRequest
	NSDictionary *headers = request.allHTTPHeaderFields;
	for (NSString *key in headers) {
		[httpRequest appendFormat:@"%@: %@\r\n", key, headers[key]];
	}
	// To simplify we don't support keepalive
	[httpRequest appendFormat:@"%s: %s\r\n", "Connection", "close"];
	
	// 6. End of Headers
	[httpRequest appendString:@"\r\n"];
	
	CFWriteStreamWrite(writeStream, (const uint8_t*)[httpRequest UTF8String],
                       [httpRequest lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
	
	// 7. HTTP Body (if available)
	if ([request HTTPBody]) {
		[httpRequest appendFormat:@"%s: %lu\r\n", "Content-Length", [[request HTTPBody] length]];
		CFWriteStreamWrite(writeStream, [[request HTTPBody] bytes], [[request HTTPBody] length]);
	}
	
	CFWriteStreamClose(writeStream);
	CFRelease(writeStream);
	writeStream = nil;
}

#pragma mark - Resolve IPv4 Address

static NSString *resolveIPv4AddressForHost(NSString *host) {
	struct addrinfo hints, *res, *res0;
	char addrStr[INET_ADDRSTRLEN];
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;  // Request IPv4 addresses
	hints.ai_socktype = SOCK_STREAM;
	
	int error = getaddrinfo([host UTF8String], NULL, &hints, &res0);
	if (error != 0) {
		NSLog(@"Error resolving IPv4 address for host: %s", gai_strerror(error));
		return nil;
	}
	
	for (res = res0; res; res = res->ai_next) {
		if (res->ai_family == AF_INET) {  // IPv4 address
			struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
			inet_ntop(AF_INET, &(ipv4->sin_addr), addrStr, INET_ADDRSTRLEN);
			freeaddrinfo(res0);
			return [NSString stringWithUTF8String:addrStr];
		}
	}
	
	freeaddrinfo(res0);
	return nil;
}

#pragma mark - Create IPv4 Socket

- (NSError*) connectIPv4Socket: (int *)fdout
{
	if (!fdout) {
		return nil;
	}
	
	// Resolve the host to IPv4
	NSString *host = [self request].URL.host;
	NSNumber *port = [self request].URL.port ?: [NSNumber numberWithInt: ([self isTLS] ? 443 : 80)];
	
	NSString *ipv4Address = resolveIPv4AddressForHost(host);
	if (!ipv4Address) {
		return [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotFindHost userInfo:nil];
	}
	
	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	
	// Prepare the address information (assuming IPv4)
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons([port intValue]);
	
	inet_pton(AF_INET, [ipv4Address UTF8String], &addr.sin_addr);
	int n = connect(sockfd, (struct sockaddr *)&addr,sizeof(addr));
	
	
	if (n < 0) {
		return [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotConnectToHost userInfo:nil];
	}
	*fdout = sockfd;
	return nil;
	
}


- (NSError *) setupStreamForSocket: (int) sockfd {
	CFStreamCreatePairWithSocket(kCFAllocatorDefault, sockfd, &readStream, &writeStream);
	
	if (!readStream || !writeStream) {
		if (readStream) {CFRelease(readStream); readStream = nil;}
		if (writeStream) {CFRelease(writeStream); writeStream = nil;}
		close(sockfd);
		return [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotConnectToHost userInfo:nil];
	}
	
	CFReadStreamSetProperty(readStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
	CFWriteStreamSetProperty(writeStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
	
	// If this is an HTTPS connection, configure SSL/TLS for the streams
	if ([self isTLS]) {
		NSDictionary *sslSettings = @{
                                      (NSString *)kCFStreamSSLLevel: (NSString *)kCFStreamSocketSecurityLevelNegotiatedSSL,
                                      (NSString *)kCFStreamSSLValidatesCertificateChain: @YES, // Validate certificate by default
                                      (NSString *)kCFStreamSSLPeerName: self.request.URL.host // Automatically use the host name in the URL
                                      };
		
		// Apply SSL settings
		CFReadStreamSetProperty(readStream, kCFStreamPropertySSLSettings, (__bridge CFTypeRef)sslSettings);
		CFWriteStreamSetProperty(writeStream, kCFStreamPropertySSLSettings, (__bridge CFTypeRef)sslSettings);
	}
	
	
	// Open the streams
	CFReadStreamOpen(readStream);
	CFWriteStreamOpen(writeStream);
	
	return nil;
}


- (void)setupReadStreamCallbacksForRunLoop: (CFRunLoopRef) runloop {
	// Define the stream callback context
	CFStreamClientContext streamContext = {0, (__bridge void *)(self), NULL, NULL, NULL};
	CFReadStreamSetClient(readStream, kCFStreamEventHasBytesAvailable | kCFStreamEventEndEncountered | kCFStreamEventErrorOccurred,
                          &readStreamCallback, &streamContext);
	CFReadStreamScheduleWithRunLoop(readStream, runloop, kCFRunLoopCommonModes);
}

void readStreamCallback(CFReadStreamRef stream, CFStreamEventType eventType, void *clientCallBackInfo) {
	IPv4OnlyHTTPProtocol *protocol = (__bridge IPv4OnlyHTTPProtocol *)clientCallBackInfo;
	switch (eventType) {
		case kCFStreamEventHasBytesAvailable: {
			UInt8 buffer[1024];
			CFIndex bytesRead = CFReadStreamRead(stream, buffer, sizeof(buffer));
			if (bytesRead > 0) {
				if (!protocol.responseHeadersReceived) {
					NSMutableData *responseBuf = [NSMutableData dataWithBytes:buffer length:bytesRead];
					NSData *headerEndMarker = [@"\r\n\r\n" dataUsingEncoding:NSUTF8StringEncoding];
					NSRange headerEndRange = [responseBuf rangeOfData:headerEndMarker options:0 range:NSMakeRange(0, bytesRead)];
					
					if (headerEndRange.location != NSNotFound) {
						NSURLResponse *response = [[NSURLResponse alloc] initWithURL:protocol.request.URL
                                                                            MIMEType:@"" expectedContentLength:-1 textEncodingName:nil];
						[protocol.client URLProtocol:protocol didReceiveResponse:response cacheStoragePolicy:NSURLCacheStorageNotAllowed];
						protocol.responseHeadersReceived = YES;
						
						NSData *bodyData = [responseBuf subdataWithRange:NSMakeRange(headerEndRange.location + headerEndRange.length, bytesRead - (headerEndRange.location + headerEndRange.length))];
						if (bodyData.length > 0) {
							[protocol.client URLProtocol:protocol didLoadData:bodyData];
						}
						
					}
					
				} else {
					NSData *data = [NSData dataWithBytes:buffer length:bytesRead];
					[protocol.client URLProtocol:protocol didLoadData:data];  // Send data to delegate
				}
				
			}
			break;
		}
		case kCFStreamEventEndEncountered: {
			// Stream ended, notify delegate that loading is done
			[protocol.client URLProtocolDidFinishLoading:protocol];
			break;
		}
		case kCFStreamEventErrorOccurred: {
			// Stream encountered an error, notify delegate
			CFErrorRef streamError = CFReadStreamCopyError(stream);
			NSError *error = (__bridge_transfer NSError *)streamError;
			[protocol.client URLProtocol:protocol didFailWithError:error];
			break;
		}
		default:
			break;
	}
}

@end
