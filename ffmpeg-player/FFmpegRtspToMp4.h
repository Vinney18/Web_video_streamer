#include "Mp4frag.h"
#include "MjpegServer.h"
#include <map>
#include <set>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/endpoint.hpp>
#include "FFmpegWrapper.h"
#include "base64.h"

using namespace std;
using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
#pragma once

CallbackFunction callback;
typedef std::set<connection_hdl, std::owner_less<connection_hdl>> con_list;
// pull out the type of messages sent by our config
typedef websocketpp::server<websocketpp::config::asio> server;
typedef server::message_ptr message_ptr;
unique_ptr<i2v::MjpegServer> mjpegServer;
map<string, con_list> m_connections;
map<string, thread> threads;
map<string, shared_ptr<i2v::MjpegRoute>> routes;
map<string, unique_ptr<Mp4frag>> mp4Parsers;
map<string, shared_ptr<FFmpegWrapper>> ffmpegList;
server echo_server;

SendSegmentCallback mp4FragCallback;

void on_open(connection_hdl hdl);
void on_close(connection_hdl hdl);
void on_message(server* s, connection_hdl hdl, message_ptr msg);

static void CallbackFromFFmpeg(uint8_t* data, int size, string id, bool isJpeg, bool SendMjpegUrl);
void SendSegment(vector<uint8_t> data, string id);

int main();
