// main.cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <fstream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socklen_t = int;
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

void initSockets() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);
#endif
}

void cleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void closeSock(int s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

// Struct pentru valori meteo
struct WeatherData {
    int temperature;
    int humidity;
    int wind;
    std::string icon;
};

// Map pentru orașe și valorile lor
std::unordered_map<std::string, WeatherData> cityWeather = {
    {"Bucharest", {25, 60, 10, "sun"}},
    {"Cluj", {20, 70, 15, "cloud"}},
    {"Timisoara", {22, 65, 12, "rain"}},
    {"Iasi", {18, 80, 8, "cloud"}}
};

std::mutex cfgMutex;
std::atomic<bool> running(true);

// Map pentru client_id -> city (înregistrări TCP)
std::mutex clientsMutex;
std::unordered_map<std::string, std::string> clientCity; // client_id -> city

// Map pentru client_id -> watchlist (vector<string>)
std::mutex favMutex;
std::unordered_map<std::string, std::vector<std::string>> clientFavs;

const std::string FAVS_FILENAME = "favs.db";

void udpBroadcastThread() {
    int udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSock < 0) {
        std::cerr << "Failed to create UDP socket\n";
        return;
    }

    int broadcast = 1;
    setsockopt(udpSock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(broadcast));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5005);
    addr.sin_addr.s_addr = INADDR_BROADCAST;

    while (running) {
        {
            std::lock_guard<std::mutex> lock(cfgMutex);
            for (auto& [city, weather] : cityWeather) {
                std::string packet = "CITY=" + city +
                                     ";TEMP=" + std::to_string(weather.temperature) +
                                     ";HUM=" + std::to_string(weather.humidity) +
                                     ";WIND=" + std::to_string(weather.wind) +
                                     ";ICON=" + weather.icon + ";";

                sendto(udpSock, packet.c_str(), (int)packet.size(), 0,
                       (sockaddr*)&addr, sizeof(addr));
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    closeSock(udpSock);
}

// Trimite reply pe socket (safely)
void sendReply(int sock, const std::string &msg) {
    if (sock >= 0) {
        send(sock, msg.c_str(), (int)msg.size(), 0);
    }
}

// Parsează mesaj SETCITY:<clientid>:<city>
bool parseSetCity(const std::string& msg, std::string& outClientId, std::string& outCity) {
    if (msg.rfind("SETCITY:", 0) != 0) return false;
    std::string rest = msg.substr(8);
    size_t p = rest.find(':');
    if (p == std::string::npos) return false;
    outClientId = rest.substr(0, p);
    outCity = rest.substr(p + 1);
    auto trim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); };
    trim(outClientId);
    trim(outCity);
    return !outClientId.empty() && !outCity.empty();
}

// Parse generic: CMD:clientid:rest    => splits into (cmd, clientid, rest)
bool parseCmdClient(const std::string &msg, std::string &cmd, std::string &clientId, std::string &rest) {
    size_t p1 = msg.find(':');
    if (p1 == std::string::npos) return false;
    cmd = msg.substr(0, p1);
    size_t p2 = msg.find(':', p1 + 1);
    if (p2 == std::string::npos) {
        // maybe no rest (e.g. GETFAV:clientid)
        clientId = msg.substr(p1 + 1);
        rest = "";
    } else {
        clientId = msg.substr(p1 + 1, p2 - (p1 + 1));
        rest = msg.substr(p2 + 1);
    }
    auto trim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); };
    trim(cmd); trim(clientId); trim(rest);
    return !cmd.empty() && !clientId.empty();
}

// Persist favorite -> file format: each line: clientid:city1,city2,...
void saveFavoritesToFile() {
    std::lock_guard<std::mutex> lock(favMutex);
    std::string tmp = FAVS_FILENAME + ".tmp";
    std::ofstream ofs(tmp, std::ios::out | std::ios::trunc);
    if (!ofs) {
        std::cerr << "Failed to open tmp favs file for writing\n";
        return;
    }
    for (const auto &p : clientFavs) {
        ofs << p.first << ":";
        for (size_t i = 0; i < p.second.size(); ++i) {
            if (i) ofs << ",";
            ofs << p.second[i];
        }
        ofs << "\n";
    }
    ofs.close();
    // atomic replace
    std::remove(FAVS_FILENAME.c_str());
    if (std::rename(tmp.c_str(), FAVS_FILENAME.c_str()) != 0) {
        std::cerr << "Failed to rename tmp favs file\n";
    } else {
        std::cout << "Favorites saved to " << FAVS_FILENAME << "\n";
    }
}

void loadFavoritesFromFile() {
    std::lock_guard<std::mutex> lock(favMutex);
    clientFavs.clear();
    std::ifstream ifs(FAVS_FILENAME);
    if (!ifs) {
        // file may not exist yet - that's ok
        std::cout << "No favorites file found, starting fresh.\n";
        return;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        size_t p = line.find(':');
        if (p == std::string::npos) continue;
        std::string cid = line.substr(0, p);
        std::string rest = line.substr(p + 1);
        std::vector<std::string> cities;
        std::stringstream ss(rest);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) cities.push_back(token);
        }
        if (!cid.empty()) clientFavs[cid] = std::move(cities);
    }
    std::cout << "Favorites loaded from " << FAVS_FILENAME << "\n";
}

void tcpServerThread() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "Failed to create TCP socket\n";
        return;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(6000);
    srv.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (sockaddr*)&srv, sizeof(srv)) < 0) {
        std::cerr << "Bind failed\n";
        closeSock(serverSock);
        return;
    }

    if (listen(serverSock, 10) < 0) {
        std::cerr << "Listen failed\n";
        closeSock(serverSock);
        return;
    }

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverSock, &readfds);
        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select(serverSock + 1, &readfds, nullptr, nullptr, &tv);
        if (sel > 0 && FD_ISSET(serverSock, &readfds)) {
            sockaddr_in client{};
            socklen_t clen = sizeof(client);
            int clientSock = accept(serverSock, (sockaddr*)&client, &clen);
            if (clientSock < 0) continue;

            std::thread([clientSock]() {
                char buffer[1024];
                int len = recv(clientSock, buffer, sizeof(buffer)-1, 0);
                if (len > 0) {
                    buffer[len] = 0;
                    std::string msg(buffer);

                    // First try SETCITY old-style
                    std::string clientId, city;
                    if (parseSetCity(msg, clientId, city)) {
                        std::lock_guard<std::mutex> lock(cfgMutex);
                        bool valid = (cityWeather.find(city) != cityWeather.end());
                        if (valid) {
                            {
                                std::lock_guard<std::mutex> lock2(clientsMutex);
                                clientCity[clientId] = city;
                            }
                            std::string reply = "CITYSET:" + city;
                            sendReply(clientSock, reply);
                            std::cout << "Client '" << clientId << "' set city '" << city << "'\n";
                        } else {
                            sendReply(clientSock, "INVALIDCITY");
                            std::cout << "Client '" << clientId << "' attempted invalid city '" << city << "'\n";
                        }
                    } else {
                        // parse generic commands: ADDFAV, REMFAV, GETFAV
                        std::string cmd, cid, rest;
                        if (parseCmdClient(msg, cmd, cid, rest)) {
                            if (cmd == "ADDFAV") {
                                std::string c = rest;
                                bool validCity = false;
                                {
                                    std::lock_guard<std::mutex> lock(cfgMutex);
                                    validCity = cityWeather.find(c) != cityWeather.end();
                                }
                                if (!validCity) {
                                    sendReply(clientSock, "INVALIDCITY");
                                } else {
                                    {
                                        std::lock_guard<std::mutex> lock(favMutex);
                                        auto &v = clientFavs[cid];
                                        // avoid duplicate
                                        if (std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
                                    }
                                    // persist to disk
                                    saveFavoritesToFile();
                                    sendReply(clientSock, std::string("FAVADDED:") + c);
                                    std::cout << "Client '" << cid << "' added favorite '" << c << "'\n";
                                }
                            } else if (cmd == "REMFAV") {
                                std::string c = rest;
                                bool removed = false;
                                {
                                    std::lock_guard<std::mutex> lock(favMutex);
                                    auto it = clientFavs.find(cid);
                                    if (it != clientFavs.end()) {
                                        auto &v = it->second;
                                        auto pos = std::find(v.begin(), v.end(), c);
                                        if (pos != v.end()) {
                                            v.erase(pos);
                                            removed = true;
                                        }
                                    }
                                }
                                if (removed) {
                                    saveFavoritesToFile();
                                    sendReply(clientSock, std::string("FAVREMOVED:") + c);
                                    std::cout << "Client '" << cid << "' removed favorite '" << c << "'\n";
                                } else {
                                    sendReply(clientSock, std::string("FAVNOTFOUND:") + c);
                                }
                            } else if (cmd == "GETFAV") {
                                std::string list;
                                {
                                    std::lock_guard<std::mutex> lock(favMutex);
                                    auto it = clientFavs.find(cid);
                                    if (it != clientFavs.end()) {
                                        for (size_t i = 0; i < it->second.size(); ++i) {
                                            if (i) list += ",";
                                            list += it->second[i];
                                        }
                                    }
                                }
                                sendReply(clientSock, std::string("FAVLIST:") + list);
                            } else {
                                sendReply(clientSock, "BADMSG");
                            }
                        } else {
                            sendReply(clientSock, "BADMSG");
                        }
                    }
                }

                closeSock(clientSock);
            }).detach();
        }
    }

    closeSock(serverSock);
}

int main() {
    initSockets();

    std::cout << "Weather Server started..." << std::endl;

    // load favorites from file at startup
    loadFavoritesFromFile();

    std::thread udp(udpBroadcastThread);
    std::thread tcp(tcpServerThread);

    std::cout << "Press ENTER to stop...";
    std::cin.get();
    running = false;

    // ensure we save current favorites one last time
    saveFavoritesToFile();

    udp.join();
    tcp.join();

    cleanupSockets();
    return 0;
}
