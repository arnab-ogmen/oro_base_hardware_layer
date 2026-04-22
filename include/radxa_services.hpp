#ifndef RADXA_SERVICES_HPP
#define RADXA_SERVICES_HPP

#include <string>
#include <nlohmann/json.hpp>

namespace oro {

class RadxaServices {
public:
    /**
     * @brief Process a host-side service command via JSON.
     * @param cmd The JSON object containing "topic" and "value".
     * @return A JSON response string representing success or failure.
     */
    static std::string process_command(const nlohmann::json& cmd);

private:
    // Helper to simulate physical work (logs and returns success)
    static std::string simulate_work(const std::string& name, float value);

    // Audio Playback Management
    static std::string handle_audio_command(int action_code);
    static void kill_playback();
    
    static int playback_pid;
    static int current_track;
    static bool is_paused;
};

} // namespace oro

#endif // RADXA_SERVICES_HPP
