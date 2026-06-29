//
//  NSURLProtocolAdditions.h
//  Transmission
//
//  Copyright (c) 2024 The Transmission Project. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <CFNetwork/CFNetwork.h>
#import <netinet/in.h>
#import <sys/socket.h>
#import <netdb.h>
#import <arpa/inet.h>

@interface IPv4OnlyHTTPProtocol : NSURLProtocol
@property (nonatomic) BOOL responseHeadersReceived;
@end

