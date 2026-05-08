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
    // Modular Command Handlers
    static std::string handle_feed(float grams);
    static std::string handle_treat_dispense(float count);
    static std::string handle_photo_capture();
    static std::string handle_live_session_start();
    static std::string handle_live_session_end();
    static std::string handle_ir_control(bool enable);
    static std::string handle_apply_settings(const nlohmann::json& settings);
    static std::string handle_firmware_update();

    // Audio Playback Management
    static std::string handle_audio_command(int action_code);
    static void kill_playback();
    
    static int playback_pid;
    static int current_track;
    static bool is_paused;
};

} // namespace oro

#endif // RADXA_SERVICES_HPP
