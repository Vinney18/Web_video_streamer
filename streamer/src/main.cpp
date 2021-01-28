#include <boost/algorithm/string_regex.hpp>
#include <boost/regex.hpp>
#include "Mp4frag.h"
#include "MjpegServer.h"
#include <map>
#include <set>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/endpoint.hpp>
#include "FFmpegWrapper.h"
#include "base64.h"
#include "json.hpp"
#include <cryptopp/aes.h>
#include <cryptopp/secblock.h>
#include <cryptopp/osrng.h>
#include <cryptopp/modes.h>
#include <cryptopp/rsa.h>
#include <cryptopp/sha.h>
#include <cryptopp/hex.h>
#include <cryptopp/filters.h>
#include <cpr/cpr.h>
#include <json/value.h>
#include <fstream>
#include "fmt/ostream.h"
#include "fmt/format.h"
#include <boost/filesystem.hpp>
#include "Options.h"
#include <ctime>
#include "json/json.h"

using namespace std;
using websocketpp::connection_hdl;
using websocketpp::lib::bind;
#pragma once

// pull out the type of messages sent by our config
typedef websocketpp::server<websocketpp::config::asio> server;
typedef server::message_ptr message_ptr;
unique_ptr<i2v::MjpegServer> mjpegServer;
map<boost::asio::detail::socket_ops::shared_cancel_token_type, string> m_connectionsIdMap;
map<string, shared_ptr<FFmpegWrapper>> ffmpegList;
server echo_server;
struct TIME
{
	int seconds;
	int minutes;
	int hours;
};

void on_open(connection_hdl hdl);
void on_close(connection_hdl hdl);
string executablePath();
string getConfigFolderPath();
void loadMainConfig();
void updateconfigFile(connection_hdl hdl , string previousServerIp, string currentServerIp);
string Get_LiveUrl(connection_hdl hdl, int cameraId, int streamtype, string analyticType);
string Get_PlayBackUrl(connection_hdl hdl, int cameraId, int start_time_ofplaybackfile, int* seekTime_ofFile, int* sessionid, bool playbackviaapache);

void on_message(server* s, connection_hdl hdl, message_ptr msg);
void SendData(websocketpp::connection_hdl& con_hndl, vector<uint8_t>& data);
void SendStringData(websocketpp::connection_hdl& con_hndl, string sdata);
string mainConfigFile;
static const std::string CONFIG_FOLDER_NAME = "config";
string serverIp = "";
int port = 8890;

const char* k = "30820222300D06092A864886F70D01010105000382020F003082020A0282020100CA6F4348BDD0963790AB94843252A34B66A6F2A8BCEE76429AD6C5F134F6779891607C2A4391BAC5E7A55B0C54D1B39D757BC7FB0C42278967A57B978F64A9748C01827B59B604D872CD0D39066AF5EFEFE8111482742AA8029E5B76449D90CA017E9542C35488CA32FFBAD8E6444DAD97E1C787ADE386FE78915D90B2D4199EAB7BDC7186F345DCC8E7929D5287D4EA536B31C8B0B326BC4330B44FDC0CF41BFA368BF4D8480A4C66E8474D0B2EFEE024B7E96332B3470EF784B37D052600A1C28237E7AE1FFA69E94EE037F282896E919F32171927BE07E83DC0A5D745997D2E698F2873BD615FA0722EE33EECE9E69187FC15956C176F6C35C541BF8FDFCB8405A5EB2549F3BBF63B547FD916219F0476A62745A192506C9AE810F0D1BD14675D08206BE4B22920F1BA1677F7E6626F1251A1486E437B2CEC82DE8992C6968574F2FC825D4609D8982D78B0D36E60E5E672C7C0A26F424ED882C1BB23DFA3CDDEF182D9BF7D3C0C850658C48D9E53447472322F6B98FAF4DEF396EDB83CCFD0A8E105F7D8ACEC69F9F02D8B75AB942E89EC58D5B9A187A9227E47B0D6C33C03E3A66779D7E090B2EE2B688964971A11C6CC5717FE2AEE60F8614C5F984F2CCBDE63B4490EB064AE6713078D7FD0D5F47427D7683F71549E95899128CAE16FB69823E5B3CFA044F3FCBB2B17553F38F79C5B2B470D69C6FEF17C55CEACD9650203010001";
bool isVerifiedUser = false;

const int MAX = 26;

// Returns a string of random alphabets of
// length n.
string printRandomString(int n)
{
	char alphabet[MAX] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g',
		'h', 'i', 'j', 'k', 'l', 'm', 'n',
		'o', 'p', 'q', 'r', 's', 't', 'u',
		'v', 'w', 'x', 'y', 'z' };

	string res = "";
	for (int i = 0; i < n; i++)
		res = res + alphabet[rand() % MAX];

	return res;
}

const std::string HexDecodeString(const std::string &string_to_decode) {

	std::string hex_decoded_string;
	CryptoPP::StringSource ss3(string_to_decode, true, new CryptoPP::HexDecoder(new CryptoPP::StringSink(hex_decoded_string)));

	return hex_decoded_string;
}

bool VerifySignature(const std::string &data, const std::string &message) {

	try {
		std::string hex_decoded_data = HexDecodeString(data);

		nlohmann::json  json_data = nlohmann::json::parse(hex_decoded_data);

#if _LICENSING_DEBUG
		std::cout << json_data.dump(4) << std::endl;
#endif
		auto license_data = json_data.at("Data").get<std::string>();
		if (license_data != message) {
			return false;
		}
		auto signature_base64 = json_data.at("Signature").get<std::string>();

		std::string signature = base64_decode(signature_base64);

		// load public key
		CryptoPP::RSA::PublicKey publicKey;
		publicKey.Load(CryptoPP::StringSource(k, true, new CryptoPP::HexDecoder()).Ref());

		// validate key
		CryptoPP::AutoSeededRandomPool rnd;
		if (!publicKey.Validate(rnd, 3)) {
			//std::cout << "Key validation failed" << std::endl;
			/*result.Error = LicenseErrorType::PublicKeyLoadError;
			result.Message = "Unable to load validate key";*/
			return false;
		}

		// verify signature
		//CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA1>::Verifier verifier(publicKey);
		CryptoPP::RSASSA_PKCS1v15_SHA_Verifier verifier(publicKey);

		bool signature_matched = verifier.VerifyMessage(reinterpret_cast<const CryptoPP::byte*>(license_data.c_str()), license_data.length(),
			reinterpret_cast<const CryptoPP::byte*>(signature.c_str()), signature.length());

		if (!signature_matched) {
			/*result.Error = LicenseErrorType::LicenseSignatureMismatch;
			result.Message = "Signature does not match";*/
		}

		return signature_matched;
	}
	catch (const std::exception & ex) {
		/*result.Error = LicenseErrorType::Exception;
		result.Message = ex.what();*/
		cout << ex.what();
	}
	return false;
}

int main()
{
	//auto isVerified = VerifySignature("7B0D0A20202244617461223A20226D797465737464617461222C0D0A2020225369676E6174757265223A2022415678643857772B6570572F4F735255663252497156686A71346C4E5839626B2F5663464A6F2B75387358776F64596667306B4656766E483253646F614238724D5670304630494865685453643776425766676B626341364F31394A54666977694C4C4B57714F442F36654671595A33325572724D376C4E77735A614B34466A55534E4F314C636A3573573658787556577972317634453157355969524A4E385366786959657367632F593D220D0A7D");
	mjpegServer = make_unique<i2v::MjpegServer>(4554);
	mjpegServer->start();
	string dir_path = getConfigFolderPath();

	boost::filesystem::path dir(dir_path);
	if (boost::filesystem::create_directory(dir)) {
		std::cout << "Success" << "\n";
	}

	mainConfigFile = getConfigFolderPath() + "/mainConf.json";
	loadMainConfig();
	// Create a server endpoint
	try {

		// Set logging settings
		echo_server.set_access_channels(websocketpp::log::alevel::all);
		echo_server.clear_access_channels(websocketpp::log::alevel::frame_payload);

		// Initialize Asio
		echo_server.init_asio();

		// Register our message handler
		echo_server.set_message_handler(bind(&on_message, &echo_server, websocketpp::lib::placeholders::_1, websocketpp::lib::placeholders::_2));
		echo_server.set_open_handler(&on_open);
		echo_server.set_close_handler(&on_close);

		// Listen on port 8181
		echo_server.listen(8181);

		// Start the server accept loop
		echo_server.start_accept();

		// Start the ASIO io_service run loop
		echo_server.run();
	}
	catch (websocketpp::exception const & e) {
		std::cout << e.what() << std::endl;
	}
	catch (exception ex) {
		std::cout << ex.what() << std::endl;
	}
	mjpegServer->stop();

}

void loadMainConfig()
{
	Options opt;
	if (boost::filesystem::exists(mainConfigFile))
	{
		opt.readFile(mainConfigFile);
		serverIp = opt.get<string>("serverIp", "");
		port = opt.get<int>("port", 8890);
	}
	else
	{
		opt.add("serverIp", "");
		opt.add("port", 8890);
		opt.writeFile(mainConfigFile, true);
	}
}

std::string getConfigFolderPath()
{
	static const std::string mainConfigFolder = fmt::format("{0}/{1}", executablePath(), CONFIG_FOLDER_NAME);
	return mainConfigFolder;
}

std::string executablePath()
{
#ifdef WIN32
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(NULL, path, MAX_PATH);
	std::wstring ws(path);
	std::string str(ws.begin(), ws.end());
	std::size_t found = str.find_last_of("/\\");;
	str = str.substr(0, found);

	return str;
#else
	return boost::filesystem::current_path().string();
#endif

}


void on_open(connection_hdl hdl) {
	try
	{
		//websocketp get_con_from_hdl();
		cout << "socket open" << endl;
		string id;
		string url;
		int start_time_ofplaybackfile = 0;
		int seekTime_ofFile = 0;
		int sessionid = 0;
		bool playbackviaapache = true;
		string analyticType = "";
		bool usejmuxer = false;
		string connectionmode = "tcp";
		int cameraId;
		int streamtype = 0;
		string mode = "Live";
		websocketpp::server<websocketpp::config::asio>::connection_ptr con = echo_server.get_con_from_hdl(hdl);
		websocketpp::uri_ptr uri = con->get_uri();
		string query = uri->get_query();
		if (!query.empty()) {
			vector<string> props;
			boost::algorithm::split_regex(props, query, boost::regex("&&"));
			for (auto const& prop : props)
			{
				vector<string> keyValue;
				boost::algorithm::split_regex(keyValue, prop, boost::regex("~~"));
				if (keyValue.size() < 2)
					continue;
				auto key = keyValue[0];
				auto value = keyValue[1];
				if (key == "id") {
					id = value;
				}
				if (key == "mode") {
					mode = value;
				}
				else if (key == "cameraId") {
					cameraId = stoi(value);
				}
				else if (key == "streamtype") {
					streamtype = stoi(value);
				}
				else if (key == "serverIp") {
					updateconfigFile(hdl, serverIp, value);
				}
				else if (key == "startTime") {
					int myint1 = stoi(value);
					start_time_ofplaybackfile = myint1;
				}
				else if (key == "useJmuxer")
				{
					usejmuxer = (value == "true");

				}
				else if (key == "connectionmode")
				{
					connectionmode = value;
				}
				else if (key == "playbackviaapache")
				{
					if (value == "1") {
						playbackviaapache = true;
					}
					else
					{
						playbackviaapache = false;
					}
				}
				else if (key == "analyticType")
				{
					analyticType = value;
				}


			}
		}

			if (mode == "Live") {
				url = Get_LiveUrl(hdl, cameraId, streamtype, analyticType);
			}
			else {
				url = Get_PlayBackUrl(hdl, cameraId, start_time_ofplaybackfile, &seekTime_ofFile, &sessionid,  playbackviaapache);
			}
			cout << url;

			if (id.empty() || url.empty()) {
				echo_server.send(hdl, "EmptyUrl", 8, websocketpp::frame::opcode::TEXT);
				echo_server.close(hdl, 0, "EmptyUrl");
				return;
			}

			try {
				if (boost::starts_with(url, "tcp"))
				{
					//TODO
					//useTranscoding = true;
				}
			}
			catch (boost::bad_lexical_cast) {
				// bad parameter
			}

			m_connectionsIdMap.insert(std::make_pair(hdl.lock(), id));

			if (ffmpegList.count(id) > 0)
			{
				/*echo_server.send(hdl, "mp4", 3, websocketpp::frame::opcode::TEXT);
				echo_server.send(hdl, mp4Parsers[id]->initialization.data(), mp4Parsers[id]->initialization.size(), websocketpp::frame::opcode::BINARY);*/
				//return;
			}
			else
			{
				auto ffmpeg = make_shared<FFmpegWrapper>(cameraId , url, id, seekTime_ofFile, &SendData, &SendStringData, usejmuxer, connectionmode, playbackviaapache, mode, start_time_ofplaybackfile, serverIp , port, sessionid);
				mjpegServer->addRoute(ffmpeg);
				ffmpegList.insert(std::make_pair(id, ffmpeg));
				ffmpeg->startThread();
			}

			ffmpegList[id]->addConnection(hdl);
	}
	catch (const std::exception & e)
	{
		cout << e.what() << std::endl;
	}
}


void updateconfigFile(connection_hdl hdl , string previousServerIp, string currentServerIp)
{
	if (currentServerIp == "")
	{
		echo_server.send(hdl, "Server_ip_not_provided", 22, websocketpp::frame::opcode::TEXT);
	}
	transform(currentServerIp.begin(), currentServerIp.end(), currentServerIp.begin(), ::tolower);
	if (currentServerIp == "localhost")
	{
		currentServerIp = "127.0.0.1";
	}
	else if (previousServerIp != currentServerIp) {
		Options opt;
		serverIp = currentServerIp;
		if (boost::filesystem::exists(mainConfigFile))
		{
			opt.readFile(mainConfigFile);
			opt.set<string>("serverIp", serverIp);
			opt.set<int>("port", port);
			opt.writeFile(mainConfigFile, true);
		}
		else {
			opt.add("serverIp", "");
			opt.add("port", 8890);
			opt.writeFile(mainConfigFile, true);
		}
	}
}

void on_close(connection_hdl hdl) {
	//hdl.lock();
	cout << "socket Closed" << endl;
	auto id = m_connectionsIdMap[hdl.lock()];
	if (id != "")
	{
		bool canStop = ffmpegList[id]->removeConnection(hdl);

		if (canStop) {
			ffmpegList[id]->stopThread();
			ffmpegList[id]->stop(); // stop route
			mjpegServer->removeRoute(id);
			ffmpegList.erase(id);
		}
		m_connectionsIdMap.erase(hdl.lock());
	}
}

void on_message(server* s, connection_hdl hdl, message_ptr msg)
{
	string messagestring = msg->get_payload();
	if (boost::starts_with(messagestring, "seek_Time"))
	{
		auto id = m_connectionsIdMap[hdl.lock()];
		if (id != "")
		{
			 string time_toseek = messagestring.substr(9);
			 int time_toseek_int  = stoi(time_toseek);
			 if (time_toseek_int >= 0)
			 {
				 ffmpegList[id]->seek_video(time_toseek_int);
			 }
		}
	}
	else if (boost::starts_with(messagestring, "Pause"))
	{
		auto id = m_connectionsIdMap[hdl.lock()];
		if (id != "")
		{
			ffmpegList[id]->Pause_video();

		}
	}
	else if (boost::starts_with(messagestring, "Resume"))
	{
		auto id = m_connectionsIdMap[hdl.lock()];
		if (id != "")
		{
			ffmpegList[id]->Resume_video();

		}
	}
	//std::cout << "on_message called with hdl: " << hdl.lock().get()
	//	<< " and message: " << msg->get_payload()
	//	<< std::endl;

	//// check for a special command to instruct the server to stop listening so
	//// it can be cleanly exited.
	//if (msg->get_payload() == "stop-listening") {
	//	s->stop_listening();
	//	return;
	//}

	//try {
	//	s->send(hdl, msg->get_payload(), msg->get_opcode());
	//}
	//catch (websocketpp::exception const & e) {
	//	std::cout << "Echo failed because: "
	//		<< "(" << e.what() << ")" << std::endl;
	//}
}

void SendData(websocketpp::connection_hdl& con_hndl, vector<uint8_t>& data) {
	try
	{
		auto dataPtr = data.data();
		auto size = data.size();
		echo_server.send(con_hndl, dataPtr, size, websocketpp::frame::opcode::BINARY);
	}
	catch (const std::exception & e)
	{
		cout << e.what();
	}
}

void SendStringData(websocketpp::connection_hdl& con_hndl, string sdata) {
	try
	{
		auto  str = sdata.c_str();
		auto  size = sdata.size();
		//echo_server.send(con_hndl, "mp4", 3, websocketpp::frame::opcode::TEXT);
		echo_server.send(con_hndl, str, size, websocketpp::frame::opcode::TEXT);
	}
	catch (const std::exception & ex)
	{
		std::cout << ex.what();
	}
}

string GetDateStringFormat_ByUnix(int start_time_ofplaybackfile) {
	time_t t = start_time_ofplaybackfile;
	struct tm* tm = localtime(&t);
	char date_string[100];
	char time_string[100];
	string am_PM = "AM";
	strftime(date_string, sizeof(date_string), "%m/%d/%Y", tm);
	strftime(time_string, sizeof(time_string), "%T", tm);
	string hour = to_string(tm->tm_hour);
	string minute = to_string(tm->tm_min);
	string second = to_string(tm->tm_sec);

	if (tm->tm_hour > 12) {
		int time = tm->tm_hour - 12;
		hour = to_string(time);
	}
	if (stoi(hour) < 10) {
		hour = "0" + hour;
	}
	if (tm->tm_min < 10) {
		minute = "0" + to_string(tm->tm_min);
	}
	if (tm->tm_sec < 10) {
		second = "0" + to_string(tm->tm_sec);
	}

	if (tm->tm_hour >= 12) {
		am_PM = "PM";
	}
	string date_string1, time_string1;
	date_string1 = string(date_string);
	time_string1 = string(time_string);
	string startTime = date_string1 + ", " + hour + ":" + minute + ":" + second + " " + am_PM;
	boost::replace_all(startTime, " ", "%20");
	return startTime;
}

string Get_PlayBackUrl(connection_hdl hdl, int cameraId, int start_time_ofplaybackfile, int* seekTime_ofFile, int*sessionid, bool streamviaapache) {
	string response = "";
	string cameraId_instring = to_string(cameraId);
	string seekVideo = "";
	string playviaapache = "false";
		if (streamviaapache) {
			playviaapache = "true";
		}
	std::string endpoint = "/url/GetPlaybackUrl?cameraId=" + cameraId_instring + "&time=" + to_string(start_time_ofplaybackfile)+"&streamviaapache=" + playviaapache;
	if (cameraId == -1) {
		response = "";
		return response;
	}
	try
	{
		std::string url = "http://" + serverIp + ":" + to_string(port) + endpoint;
		auto res = cpr::Get(cpr::Url{ url });

		if (res.status_code == 200) {
			string json = res.text;
			Json::Reader reader;
			Json::Value root;
			bool parseSuccess = reader.parse(json, root, false);
			if (parseSuccess)
			{
				Json::Value resultValue = root["GetEventPlaybackUrlResult"];
				if (resultValue.asString() == "") {
					resultValue = root["getEventPlaybackUrlResult"];
				}
				Json::Value resultValue1 = root["Seek_Time_InSeconds"];
				if (resultValue1.asString() == "") {
					resultValue1 = root["seek_Time_InSeconds"];
				}
				Json::Value resultValue2 = root["SessionId"];
				if (resultValue2.asString() == "") {
					resultValue2 = root["sessionId"];
				}

				*sessionid = stoi(resultValue2.asString());
				*seekTime_ofFile = stoi(resultValue1.asString());

				response = resultValue.asString();
				response.erase(std::remove(response.begin(), response.end(), '\"'), response.end());
				response.erase(std::remove(response.begin(), response.end(), '\\'), response.end());
			}
			else if (res.status_code == 403) {
				cout << "Server License Expired \n";
				echo_server.send(hdl, "License Expired", 15, websocketpp::frame::opcode::TEXT);
			}
			else if (res.status_code == 400) {
				cout << "Some problem occured \n";
				echo_server.send(hdl, "Some problem occured", 20, websocketpp::frame::opcode::TEXT);
			}
			else {
				response = "";
			}
		}
		else {
			response = "";
		}
	}
	catch (const std::exception & e)
	{
		response = "";
	}
	return response;
}

string Get_LiveUrl(connection_hdl hdl, int cameraId, int streamtype, string analyticType) {

	string response = "";
	string cameraId_instring = to_string(cameraId);

	std::string endpoint = "/url/GetLiveUrl?cameraId=" + cameraId_instring + "&streamType=" + to_string(streamtype) + "&analyticType=" + analyticType;
	if (cameraId == -1) {
		response = "";
		return response;
	}
	try
	{
		std::string url = "http://" + serverIp + ":" + to_string(port) + endpoint;
		auto res = cpr::Get(cpr::Url{ url });

		if (res.status_code == 200) {
			string command = res.text;
			string c = "\\";
			command.erase(std::remove(command.begin(), command.end(), '\"'), command.end());
			command.erase(std::remove(command.begin(), command.end(), '\\'), command.end());

			response = command;
		}
		else if (res.status_code == 403) {
			cout << "Server License Expired \n";
			echo_server.send(hdl, "License Expired", 15, websocketpp::frame::opcode::TEXT);
		}
		else if (res.status_code == 400) {
			cout << "Some problem occured \n";
			echo_server.send(hdl, "Some problem occured", 20, websocketpp::frame::opcode::TEXT);
		}
		else {
			response = "";
		}
	}
	catch (const std::exception & e)
	{
		response = "";
	}
	return response;
}
