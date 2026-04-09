#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>

class FFmpegWrapper;  // forward declaration

struct SyncGroupInfo {
	std::unordered_set<std::string> clientIds;
	std::unordered_set<FFmpegWrapper*> ffmpegRefs;
	std::mutex mutex;
	std::condition_variable cv;
	bool released = false;
	std::chrono::steady_clock::time_point createdAt;
};

class SyncHandler {
public:
	SyncHandler();
	~SyncHandler();

	// Called from createPeerConnection() — creates group if needed, adds clientId to the group
	void incrementExpectedCount(const std::string& groupId, const std::string& clientId);

	// Called from FFmpegWrapper::run() before readInput().
	// Inserts ffmpeg ref into group's set, then:
	//   - If set.size() == requestedCount → notify all, return immediately
	//   - Else → block on CV until released or timeout
	void waitForReady(const std::string& groupId, FFmpegWrapper* ffmpeg);

	// Called when a connection fails before FFmpeg is created — removes only the clientId.
	void removeClient(const std::string& groupId, const std::string& clientId);

	// Called when a single member disconnects — removes its FFmpeg ref and clientId from the group.
	// If the group becomes empty, removes the group entirely.
	// If the group was waiting (not yet released) and remaining members now satisfy the count, releases.
	void removeMember(const std::string& groupId, const std::string& clientId, FFmpegWrapper* ffmpeg);

	// Called on full cleanup — releases waiting threads and removes entire group
	void removeGroup(const std::string& groupId);

private:
	friend class WebRTCWrapper;
	std::unordered_map<std::string, std::shared_ptr<SyncGroupInfo>> groups_;
	std::mutex groupsMutex_;

	std::shared_ptr<spdlog::logger> logger_;

	static constexpr int TIMEOUT_SECONDS = 30;
};
