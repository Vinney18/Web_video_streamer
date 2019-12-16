/*******************************************************
* Copyright (C) i2V Systems Pvt. Ltd.
* All Rights Reserved
*
* NOTICE:  All information contained herein is, and remains
* the property of i2V Systems Pvt. Ltd. The intellectual and technical concepts contained
* herein are proprietary to i2V Systems Pvt. Ltd. and are protected by trade secret or copyright law.
* Dissemination of this information or reproduction of this material
* is strictly forbidden unless prior written permission is obtained
* from i2V Systems Pvt. Ltd.
*
* If you receive copy of this file from any source other than i2V Systems Pvt. Ltd.
* please report at i2v@i2vsys.com
*
*******************************************************/

#pragma once
#ifndef ANALYTIC_SERVER_HTTP_SERVER
#define ANALYTIC_SERVER_HTTP_SERVER

#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/MediaType.h>
#include <Poco/BasicEvent.h>

#include <utility>
#include <algorithm>
#include <deque>
#include "safe_ptr.h"
typedef unsigned char uchar;
#define STREAMER_FRAME_QUEUE_SIZE 5

using Poco::Net::HTTPRequestHandler;
using Poco::Net::HTTPServerRequest;
using Poco::Net::HTTPServerResponse;
using Poco::Net::HTTPRequestHandlerFactory;
using Poco::Net::HTTPServerParams;
using Poco::Net::HTTPServer;
using Poco::Net::ServerSocket;
using Poco::Net::SocketAddress;
using Poco::BasicEvent;

namespace i2v {


	class EncodedFrameQueue
	{
	public:
		EncodedFrameQueue(std::size_t maxSize) : _maxSize(maxSize) {};
		virtual ~EncodedFrameQueue();

		void push(std::vector<uchar> endocedFrame)
		{
			_encodeFrames->push_back(endocedFrame);

			while (_encodeFrames->size() > _maxSize)
			{
				_encodeFrames->pop_front();
			}
		}

		std::vector<uchar> pop()
		{
			std::vector<uchar> endocedFrame;
			if (!_encodeFrames->empty())
			{
				endocedFrame = _encodeFrames->front();
				_encodeFrames->pop_front();
				return endocedFrame;
			}
			else
			{
				return endocedFrame;
			}
		}

		std::size_t getMaxSize() const { return _maxSize; }
		std::size_t size() const { return _encodeFrames->size(); }
		bool empty() const { return _encodeFrames->empty(); }
		void clear() { _encodeFrames->clear(); }

	private:
		sf::safe_ptr< std::deque<std::vector<uchar>> > _encodeFrames;
		std::size_t _maxSize;

	};

	enum class MjpegConnectionState : int {
		NORMAL = 1,
		FAULTED = 2
	};

	class MjpegRoute
	{
	public:
		MjpegRoute(std::string route_uri) : mRouteUri(std::move(route_uri)) {}
		virtual ~MjpegRoute();
		BasicEvent<std::vector<uchar>> newFrame;
		BasicEvent<void> routeRemoved;
		void increaseConnectionCount() { connection_count++; }
		void decreaseConnectionCount() {
			connection_count--;
			if (connection_count < 0) connection_count = 0;
		}

	protected:
		std::string mRouteUri;
		unsigned int connection_count = 0;

	public:
		std::string getRouteUri() const { return mRouteUri; }
		bool equals(const std::string& route_uri) const { return (mRouteUri == route_uri); }

		void send(const std::vector<uchar>& encoded_frame);
		void stop();
	};

	class MjpegConnection : public HTTPRequestHandler, public EncodedFrameQueue
	{

	public:
		MjpegConnection(std::shared_ptr<MjpegRoute>);
		virtual ~MjpegConnection();
		void stop();
		void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) override;
		MjpegConnectionState getState() const { return mState; }

		void onFrameReceive(const void* sender, std::vector<uchar>& endcodedFrame) { push(endcodedFrame); }
		void onRouteRemove(const void* sender) { stop(); }

	private:
		bool keepRunning;
		MjpegConnectionState mState;
		std::shared_ptr<MjpegRoute> _route;
	};

	class RouteAlreadyExists : std::runtime_error
	{
	public:
		RouteAlreadyExists(const std::string& message) : std::runtime_error(message.c_str()) {}
	};

	class MjpegRouteHandler : public HTTPRequestHandlerFactory
	{

		typedef std::map<std::string, std::shared_ptr<MjpegRoute>> MjpegRouteMap;
	private:
		std::mutex mRouteMutex;
		MjpegRouteMap mRoutes;

	public:
		~MjpegRouteHandler();
		/**
		* adds a provided route, but if a route with the same uri already exists then throws RouteAlreadyExists exception
		*/
		bool addRoute(std::shared_ptr<MjpegRoute> route);
		bool removeRoute(const std::string& routeUri);

		HTTPRequestHandler* createRequestHandler(const HTTPServerRequest& request) override;

	};

	class MjpegServer;

	typedef std::shared_ptr<MjpegServer> MjpegServerPtr;
	typedef std::map<std::string, MjpegServerPtr> MjpegServermap;

	class MjpegServer {
	public:

		MjpegServer(unsigned short port);
		~MjpegServer() = default;

		bool start();
		void stop();
		bool addRoute(std::shared_ptr<MjpegRoute> route) const;
		bool removeRoute(const std::string& routeUri) const;

		bool isRunning() const { return mIsRunning; }

		static MjpegServerPtr getServer(const std::string& name);
		static bool addServer(std::string name, MjpegServerPtr server);
		static bool removeServer(const std::string& name);

	private:
		static MjpegServermap mServers;
		unsigned short mPort;
		bool mIsRunning;

		std::shared_ptr<Poco::Net::HTTPServer> mServer;
		HTTPRequestHandlerFactory::Ptr mMjpegRouteHandler;
	};

}
#endif