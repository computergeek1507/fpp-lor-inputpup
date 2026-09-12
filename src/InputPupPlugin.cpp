#include <fpp-pch.h>

#include <unistd.h>
#include <ifaddrs.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <cstring>
#include <fstream>
#include <list>
#include <vector>
#include <sstream>
#include <cmath>
#include <mutex>
#include <regex>
#include <thread>
#include <chrono>
#include <future>
#include <atomic>
#include <array>
#include <cstdint>

#include <termios.h>

#include "commands/Commands.h"
#include "common.h"
#include "settings.h"
#include "Plugin.h"
#include "log.h"
#include "fpphttp.h"

#include "channeloutput/serialutil.h"

struct SerialCondition {
    SerialCondition() {}
    explicit SerialCondition(Json::Value &v) {
        if (v.isMember("condition")) {
            conditionType = v["condition"].asString();
        }
        if (v.isMember("conditionValue")) {
            val = v["conditionValue"].asString();
        }    
    }
    
    bool matches(std::string const& ev) {
        if (conditionType == "contains") {
            return ev.find(val) != std::string::npos;
        }
        if (conditionType == "startswith") {
            return ev.starts_with(val);
        }
        if (conditionType == "endswith") {
            return ev.ends_with(val);
        }
        if (conditionType == "regex") {
            try{
                std::regex self_regex(val, std::regex_constants::ECMAScript | std::regex_constants::icase);
                return std::regex_search(ev, self_regex);
            } catch(std::exception &ex) {
                LogErr(VB_PLUGIN, "Regex Error '%s'\n", ex.what());
                return false;
            }
        } 
        return false;
    }    
    std::string conditionType = "contains";
    std::string val;
};

struct SerialModifier {
    SerialModifier() {}
    explicit SerialModifier(Json::Value &v) {
        if (v.isMember("modifier")) {
           modifierType = v["modifier"].asString();
        }
        if (v.isMember("modifierValue")) {
            modifierValue = v["modifierValue"].asString();   
        }     
    }
    
    std::string modify(std::string value) {
        if (modifierType == "none") {            
            return value;
        } 
        if (modifierType == "substring") {
            int start = 0;
            int length = -1;
            try {
                if(!modifierValue.empty()) {
                    if(modifierValue.find(",") != std::string::npos) {
                        auto values = split(modifierValue, ',');
                        if(values.size() == 2) { 
                            start = stoi(values[0]);
                            length = stoi(values[1]);
                        } else if(values.size() == 1) {
                            length = stoi(values[0]);
                        }
                    } 
                }
            } catch(std::exception &ex) {
                LogErr(VB_PLUGIN, "Modifier Syntax Error '%s' '%s'\n", modifierValue.c_str(),  ex.what());
            }
            return value.substr(start, length);   
        } 
        if (modifierType == "regex") {
            try{
                std::regex regex(modifierValue, std::regex_constants::ECMAScript | std::regex_constants::icase);
                std::smatch match;
                if (std::regex_match(value, match, regex))
                {
                    // The first sub_match is the whole string; the next
                    // sub_match is the first parenthesized expression.
                    if (match.size() == 2)
                    {
                        std::ssub_match sub_match = match[1];
                        std::string match_text = sub_match.str();
                        //std::cout << value << " has a match of " << match_text << '\n';
                        return match_text;
                    }
                }
            } catch(std::exception &ex) {
                LogErr(VB_PLUGIN, "Regex Error '%s'\n", ex.what());
            }
        } 
        return value;
    }    
    std::string modifierType = "none";
    std::string modifierValue;
};

struct SerialCommandArg {
    explicit SerialCommandArg(const std::string &t) : arg(t) { }
    ~SerialCommandArg() { }    
    std::string arg;
    std::string type;
};


struct SerialEvent {
    explicit SerialEvent(Json::Value &v) {
        description = v["description"].asString();
        condition = SerialCondition(v);
        modifier = SerialModifier(v);

        command = v;
        command.removeMember("argTypes");
        command.removeMember("args");
        command.removeMember("condition");
        command.removeMember("conditionValue");
        command.removeMember("modifier");
        command.removeMember("modifierValue");
        command.removeMember("description");

        if (v.isMember("args")) {
            for (int x = 0; x < v["args"].size(); x++) {
                args.push_back(SerialCommandArg(v["args"][x].asString()));
            }
        }
        if (v.isMember("argTypes")) {
            for (int x = 0; x < v["argTypes"].size(); x++) {
                args[x].type = v["argTypes"][x].asString();
            }
        }
    }
    ~SerialEvent() {
        args.clear();
    }
    
    bool matches(std::string const& ev) {
        return condition.matches(ev);
    }

    std::string modify(std::string ev) {
        return modifier.modify(ev);
    }
    
    void invoke(std::string ev) {       
        Json::Value newCommand = command;
        for (auto &a : args) {
            std::string tp = "string";
            if (a.type == "bool" || a.type == "int") {
                tp = a.type;
            }
            
            //printf("Eval p: %s\n", a.arg.c_str());
            std::string r = a.arg;

            if(tp == "string") {
                if(r.find("%VAL%") != std::string::npos) {
                    replaceAll(r, "%VAL%" , ev);
                }
            }
            //printf("        -> %s\n", r.c_str());
            newCommand["args"].append(r);
        }

        CommandManager::INSTANCE.run(newCommand);
    }    

    std::string description;    
    SerialCondition condition;
    SerialModifier modifier;
    
    Json::Value command;
    std::vector<SerialCommandArg> args;
};

// LOR "Input Pup" controllers speak the same heartbeat/poll protocol LOR
// network devices use for status. Reference implementation:
// https://github.com/xLightsSequencer/xSchedule/blob/main/xSchedule/events/ListenerLor.cpp
enum class LorRcvState {
    WAITING_FOR_FE,
    WAITING_FOR_65,
    WAITING_FOR_BYTE1,
    WAITING_FOR_BYTE2,
    PROCESS_DATA
};

static const char* SERIALEVENT_API_PATH = "/SERIALEVENT";

class InputPupPlugin : public FPPPlugin {
public:
    std::vector<std::unique_ptr<SerialEvent>> serial_events;
    std::list<std::string> serial_data;
    std::mutex serial_data_mutex;

    int m_fd {-1};
    bool enabled {false};
    int m_unitId {1};

    std::thread m_pollThread;
    std::atomic<bool> m_stopThread {false};

    std::mutex m_inputsMutex;
    std::array<bool, 8> m_lastInputs {};
    bool m_haveBaseline {false};

    InputPupPlugin() : FPPPlugin("fpp-lor-inputpup") {
        LogInfo(VB_PLUGIN, "Initializing LOR Input Pup Plugin\n");
        enabled = InitSerial();
        if (enabled) {
            m_pollThread = std::thread(&InputPupPlugin::PollLoop, this);
        }
    }
    virtual ~InputPupPlugin() {
        StopPolling();
        CloseSerial();
    }

    // Called by FPP once its HTTP routes have been disarmed and before the
    // plugin object is destroyed. Threads must be stopped and joined here,
    // not just in the destructor, since a call can still be in flight against
    // a half-destroyed object otherwise.
    virtual std::function<bool()> shutdown() override {
        StopPolling();
        CloseSerial();
        return nullptr;
    }

    void StopPolling() {
        m_stopThread = true;
        if (m_pollThread.joinable()) {
            m_pollThread.join();
        }
    }
    bool InitSerial() {
        if (FileExists(FPP_DIR_CONFIG("/plugin.lor-inputpup.json"))) {
            std::string port;
            int speed = 115200;
            try {
                Json::Value root;
                bool success =  LoadJsonFromFile(FPP_DIR_CONFIG("/plugin.lor-inputpup.json"), root);
                if (root.isMember("serialEvents")) {
                    for (int x = 0; x < root["serialEvents"].size(); x++) {
                        serial_events.emplace_back(std::make_unique<SerialEvent>(root["serialEvents"][x]));
                    }
                }

                if (root.isMember("port")) {
                    port = root["port"].asString();
                }
                if (root.isMember("speed")) {
                    speed = root["speed"].asInt();
                }
                if (root.isMember("unitId")) {
                    m_unitId = (int)std::strtol(root["unitId"].asString().c_str(), nullptr, 16);
                }
                if (m_unitId <= 0) {
                    m_unitId = 1;
                }
                LogInfo(VB_PLUGIN, "Using %s Serial Output Speed %d, Unit Id 0x%02X\n", port.c_str(), speed, m_unitId);
                if(port.empty()) {
                    LogErr(VB_PLUGIN, "Serial Port is empty '%s'\n", port.c_str());
                    return false;
                }
                if(port.find("/dev/") == std::string::npos)
                {
                    port = "/dev/" + port;
                }
                // Need read/write access to send heartbeat/poll requests and
                // read the responses, so this must be opened as an output
                // (O_RDWR) port rather than a read-only input port.
                int fd = SerialOpen(port.c_str(), speed, "8N1", true);
                if (fd < 0) {
                    LogErr(VB_PLUGIN, "Could Not Open Serial Port '%s'\n", port.c_str());
                    return false;
                }
                m_fd = fd;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                tcflush(m_fd,TCIOFLUSH);
                ioctl(m_fd, TCIOFLUSH, 2);
                LogInfo(VB_PLUGIN, "Serial Input Started\n");
                return true;
            } catch (...) {
                LogErr(VB_PLUGIN, "Could not Initialize Serial Port '%s'\n", port.c_str());
            }
        }else{
            LogInfo(VB_PLUGIN, "No plugin.lor-inputpup.json config file found\n");
        }
        return false;
    }

    void CloseSerial() {
        if (m_fd >= 0) {
            SerialClose(m_fd);
            m_fd = -1;
        }
    }

    int SerialDataRead(int fd, char* buf, size_t len) {
        // Read() (using read() ) will return an 'error' EAGAIN as it is
        // set to non-blocking. This is not a true error within the
        // functionality of Read, and thus should be handled by the caller.
        int n = read(fd, buf, len);
        if((n < 0) && (errno == EAGAIN)) return 0;
        return n;
    }

    void WriteBytes(const uint8_t *data, size_t len) {
        ssize_t written = write(m_fd, data, len);
        if (written < 0) {
            LogErr(VB_PLUGIN, "Error writing to serial port: %s\n", strerror(errno));
        }
    }

    void SendHeartbeat() {
        const uint8_t d[5] = { 0x00, 0xFF, 0x81, 0x56, 0x00 };
        WriteBytes(d, sizeof(d));
    }

    void SendPollRequest(int unitId) {
        const uint8_t d[6] = { 0x00, (uint8_t)unitId, 0x88, 0x64, 0x2D, 0x00 };
        WriteBytes(d, sizeof(d));
    }

    void SendPollAck(int unitId) {
        const uint8_t d[6] = { 0x00, (uint8_t)unitId, 0x88, 0x69, 0x2D, 0x00 };
        WriteBytes(d, sizeof(d));
    }

    // Sends a poll request for unitId and waits up to 50ms for the
    // "FE 65 <inputs1> <inputs2>" response, mirroring xSchedule's
    // ListenerLor::Poll(). Returns true and fills inputs1/inputs2 if a
    // full response was received in time.
    bool PollUnit(int unitId, uint8_t &inputs1, uint8_t &inputs2) {
        SendPollRequest(unitId);

        LorRcvState state = LorRcvState::WAITING_FOR_FE;
        uint8_t buf[128];
        auto start = std::chrono::steady_clock::now();

        while (!m_stopThread) {
            int n = SerialDataRead(m_fd, (char*)buf, sizeof(buf));
            for (int i = 0; i < n; i++) {
                uint8_t b = buf[i];
                switch (state) {
                    case LorRcvState::WAITING_FOR_FE:
                        if (b == 0xFE) state = LorRcvState::WAITING_FOR_65;
                        break;
                    case LorRcvState::WAITING_FOR_65:
                        if (b == 0x65) state = LorRcvState::WAITING_FOR_BYTE1;
                        break;
                    case LorRcvState::WAITING_FOR_BYTE1:
                        if (b & 0x80) {
                            inputs1 = b;
                            state = LorRcvState::WAITING_FOR_BYTE2;
                        }
                        break;
                    case LorRcvState::WAITING_FOR_BYTE2:
                        if (b & 0x80) {
                            inputs2 = b;
                            state = LorRcvState::PROCESS_DATA;
                        }
                        break;
                    default:
                        break;
                }
            }
            if (state == LorRcvState::PROCESS_DATA) {
                break;
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed >= 50) {
                break;
            }
            if (n <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }

        if (state == LorRcvState::PROCESS_DATA) {
            SendPollAck(unitId);
            return true;
        }
        return false;
    }

    void ProcessInputs(int unitId, uint8_t inputs1, uint8_t inputs2) {
        std::array<bool, 8> inputs {
            (inputs1 & 0x08) != 0,
            (inputs1 & 0x04) != 0,
            (inputs1 & 0x02) != 0,
            (inputs1 & 0x01) != 0,
            (inputs2 & 0x08) != 0,
            (inputs2 & 0x04) != 0,
            (inputs2 & 0x02) != 0,
            (inputs2 & 0x01) != 0,
        };

        std::array<bool, 8> changed {};
        bool anyChanged = false;
        {
            std::lock_guard<std::mutex> lock(m_inputsMutex);
            if (!m_haveBaseline) {
                // Establish the starting state without firing events for
                // whatever the inputs happen to be at plugin startup.
                m_lastInputs = inputs;
                m_haveBaseline = true;
                return;
            }

            for (size_t i = 0; i < inputs.size(); i++) {
                if (inputs[i] != m_lastInputs[i]) {
                    changed[i] = true;
                    anyChanged = true;
                }
            }
            m_lastInputs = inputs;
        }

        if (anyChanged) {
            for (size_t i = 0; i < inputs.size(); i++) {
                if (changed[i]) {
                    FireEvent(unitId, (int)i + 1, inputs[i]);
                }
            }
        }
    }

    // Returns a JSON snapshot of the current input states, e.g.
    // {"unitId":1,"haveData":true,"inputs":[0,1,0,0,0,0,0,0]}
    std::string GetStatusJson() {
        Json::Value root;
        root["unitId"] = m_unitId;

        std::lock_guard<std::mutex> lock(m_inputsMutex);
        root["haveData"] = m_haveBaseline;
        for (size_t i = 0; i < m_lastInputs.size(); i++) {
            root["inputs"].append(m_lastInputs[i] ? 1 : 0);
        }
        return SaveJsonToString(root);
    }

    void FireEvent(int unitId, int input, bool pressed) {
        std::string ev = "LOR:" + std::to_string(unitId) + ":" + std::to_string(input) + ":" + (pressed ? "1" : "0");
        LogInfo(VB_PLUGIN, "LOR Input Pup event '%s'\n", ev.c_str());

        {
            std::lock_guard<std::mutex> lock(serial_data_mutex);
            serial_data.push_back(ev);
            if (serial_data.size() > 25) {
                serial_data.pop_front();
            }
        }

        for (auto &a : serial_events) {
            if (a->matches(ev)) {
                auto text = a->modify(ev);
                a->invoke(text);
            }
        }
    }

    void PollLoop() {
        auto lastHeartbeat = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        auto lastPoll = std::chrono::steady_clock::now() - std::chrono::seconds(1);

        while (!m_stopThread) {
            auto now = std::chrono::steady_clock::now();

            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeat).count() >= 500) {
                lastHeartbeat = now;
                SendHeartbeat();
            }

            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPoll).count() >= 100) {
                lastPoll = now;
                uint8_t inputs1 = 0, inputs2 = 0;
                if (PollUnit(m_unitId, inputs1, inputs2)) {
                    ProcessInputs(m_unitId, inputs1, inputs2);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void HandleApi(const HttpRequestPtr &req, HttpCallback &&callback) {
        auto pieces = getPathPieces(req->path());

        // pieces[0] is "SERIALEVENT"; a subpath (family route) lands here as
        // pieces[1], e.g. "/SERIALEVENT/list" -> pieces = {"SERIALEVENT", "list"}.
        if (pieces.size() > 1) {
            const std::string &p1 = pieces[1];
            if (p1 == "list") {
                std::string v;
                std::lock_guard<std::mutex> lock(serial_data_mutex);
                for (auto &sd : serial_data) {
                    v += sd + "\n";
                }
                callback(makeStringResponse(v, 200));
                return;
            }
            if (p1 == "status") {
                callback(makeStringResponse(GetStatusJson(), 200, "application/json"));
                return;
            }
        }
        callback(makeStringResponse("Not Found", 404));
    }

    void registerApis() override {
        FPPPlugins::registerPluginApi(
            SERIALEVENT_API_PATH,
            [this](const HttpRequestPtr &req, HttpCallback &&callback) {
                HandleApi(req, std::move(callback));
            },
            { drogon::Get },
            true);
    }

    void unregisterApis() override {
        FPPPlugins::unregisterPluginApi(SERIALEVENT_API_PATH);
    }
};


extern "C" {
    FPPPlugin *createPlugin() {
        return new InputPupPlugin();
    }
}
