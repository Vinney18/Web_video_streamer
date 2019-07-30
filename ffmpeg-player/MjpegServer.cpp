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

#include "MjpegServer.h"
#include <iso646.h>
#include "Poco/Delegate.h"

using namespace i2v;

i2v::MjpegServermap i2v::MjpegServer::mServers{};


MjpegConnection::MjpegConnection(std::shared_ptr<MjpegRoute> route) : EncodedFrameQueue(STREAMER_FRAME_QUEUE_SIZE), keepRunning(true), mState(MjpegConnectionState::NORMAL) {
	_route = route;
}


i2v::MjpegConnection::~MjpegConnection()
{
	stop();
}

void i2v::MjpegConnection::stop()
{
	keepRunning = false;
	clear();
}

void i2v::MjpegConnection::handleRequest(HTTPServerRequest & request, HTTPServerResponse & response)
{
	try
	{
		_route->newFrame += Poco::delegate(this, &MjpegConnection::onFrameReceive);
		_route->routeRemoved += Poco::delegate(this, &MjpegConnection::onRouteRemove);
		_route->increaseConnectionCount();

		Poco::Net::MediaType mediaType("multipart/x-mixed-replace");
		mediaType.setParameter("boundary", "--boundary");
		response.set("Cache-control", "no-cache, no-store, must-revalidate");
		response.set("Pragma", "no-cache");
		response.set("Server", "testVideoServer");
		response.setContentType(mediaType);

		std::ostream& outputStream = response.send();

		while (keepRunning)
		{
			if (outputStream.good() && !outputStream.fail() && !outputStream.bad())
			{
				std::vector<uchar> encoded = pop();

				if (not encoded.empty())
				{
					outputStream << "--boundary";
					outputStream << "\r\n";
					outputStream << "Content-Type: image/jpeg";
					outputStream << "\r\n";
					outputStream << "Content-Length: " << encoded.size();
					outputStream << "\r\n";
					outputStream << "\r\n";
					outputStream.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());

					outputStream.flush();
				}
			}
			else
			{
				throw Poco::Exception("Response stream failed or went bad -- it was probably interrupted.");
			}
			Poco::Thread::sleep(10);
		}
	}

	catch (const Poco::Exception& ex)
	{
		mState = MjpegConnectionState::FAULTED;
		//std::cout << "MjpegRequestHandler::handleRequest error: " << ex.what() << std::endl;
	}
	catch (const std::exception& exception)
	{
		mState = MjpegConnectionState::FAULTED;
		//std::cout << "MjpegRequestHandler::handleRequest error: " << exception.what() << std::endl;
	}

	_route->newFrame -= Poco::delegate(this, &MjpegConnection::onFrameReceive);
	_route->routeRemoved -= Poco::delegate(this, &MjpegConnection::onRouteRemove);
	_route->decreaseConnectionCount();
}

i2v::MjpegRoute::~MjpegRoute()
{
	stop();
}

void i2v::MjpegRoute::send(const std::vector<uchar>& encoded_frame)
{
	std::vector<uchar> d = encoded_frame;
	newFrame.notify(this, d);
}

void i2v::MjpegRoute::stop()
{
	routeRemoved();
}

i2v::MjpegRouteHandler::~MjpegRouteHandler()
{
	std::lock_guard<std::mutex> guard(mRouteMutex);
	MjpegRouteMap::iterator it;
	for (it = mRoutes.begin(); it not_eq mRoutes.end(); it++)
	{
		it->second->stop();
	}
	mRoutes.clear();
}

bool i2v::MjpegRouteHandler::addRoute(std::shared_ptr<MjpegRoute> route)
{
	std::string routeUri = route->getRouteUri();
	// check if route already exists
	std::lock_guard<std::mutex> guard(mRouteMutex);
	if (mRoutes.find(routeUri) == mRoutes.end())
	{
		mRoutes.insert(std::make_pair(routeUri, route));
	}
	else
	{
		throw RouteAlreadyExists("Route already exists");
	}
	return true;
}

bool i2v::MjpegRouteHandler::removeRoute(const std::string & routeUri)
{
	std::lock_guard<std::mutex> guard(mRouteMutex);
	MjpegRouteMap::iterator it = mRoutes.find(routeUri);
	if (it not_eq mRoutes.end())
	{
		it->second->stop();
		mRoutes.erase(routeUri);
	}
	return true;
}

HTTPRequestHandler* i2v::MjpegRouteHandler::createRequestHandler(const HTTPServerRequest & request)
{
	HTTPRequestHandler* handler = nullptr;
	const std::string& request_uri = request.getURI();

	std::lock_guard<std::mutex> guard(mRouteMutex);
	MjpegRouteMap::iterator it;
	for (it = mRoutes.begin(); it not_eq mRoutes.end(); it++)
	{
		if ("/" + it->first == request_uri)
		{
			handler = new MjpegConnection(it->second);
			break;
		}
	}
	return handler;
}


i2v::MjpegServer::MjpegServer(unsigned short port) : mPort(port), mIsRunning(false)
{
}

bool i2v::MjpegServer::start()
{
	Poco::Net::ServerSocket serverSocket(SocketAddress("0.0.0.0", mPort));
	HTTPServerParams* pParams = new HTTPServerParams;
	// ... set server params here to your liking

	mMjpegRouteHandler = HTTPRequestHandlerFactory::Ptr(new MjpegRouteHandler);
	mServer = std::make_shared<Poco::Net::HTTPServer>(mMjpegRouteHandler, serverSocket, pParams);
	mServer->start(); // NB: server will fly off here (to its own thread)
	mIsRunning = true;
	return true;
}

void i2v::MjpegServer::stop()
{
	if (mServer)
	{
		// stop server disconnect all client connections
		mServer->stopAll(true);
	}
	mIsRunning = false;

}

bool i2v::MjpegServer::addRoute(std::shared_ptr<MjpegRoute> route) const
{
	Poco::SharedPtr<MjpegRouteHandler> ptr = mMjpegRouteHandler.unsafeCast<MjpegRouteHandler>();
	return ptr->addRoute(route);
}

bool i2v::MjpegServer::removeRoute(const std::string & routeUri) const
{
	Poco::SharedPtr<MjpegRouteHandler> ptr = mMjpegRouteHandler.unsafeCast<MjpegRouteHandler>();
	return ptr->removeRoute(routeUri);
}

i2v::MjpegServerPtr i2v::MjpegServer::getServer(const std::string & name)
{
	MjpegServermap::iterator it = mServers.find(name);
	if (it not_eq mServers.end())
	{
		return it->second;
	}
	return nullptr;
}

bool i2v::MjpegServer::addServer(std::string name, MjpegServerPtr server)
{
	mServers.insert(std::make_pair(name, server));
	return true;
}

bool i2v::MjpegServer::removeServer(const std::string & name)
{
	MjpegServermap::iterator it = mServers.find(name);
	if (it not_eq mServers.end())
	{
		mServers.erase(it);
	}
	return true;
}

i2v::EncodedFrameQueue::~EncodedFrameQueue()
{
}
