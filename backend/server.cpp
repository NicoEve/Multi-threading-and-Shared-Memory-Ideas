#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <memory>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 5000
#define GAME_WIDTH 640
#define GAME_HEIGHT 840

// Coordenadas X de los 6 carriles posibles (calculados exactamente como en el frontend)
const int LANE_POSITIONS[6] = { 160, 224, 288, 352, 416, 480 };
const int TOTAL_LANES = 6;

// ============================================================================
// Utilidades Criptográficas y de Red para WebSocket RFC 6455
// ============================================================================

struct SHA1 {
    uint32_t state[5];
    uint32_t count;
    uint8_t buffer[64];

    static uint32_t rol(uint32_t value, size_t bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    void transform(const uint8_t data[64]) {
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) |
                   ((uint32_t)data[i * 4 + 2] << 8) | ((uint32_t)data[i * 4 + 3]);
        }
        for (int i = 16; i < 80; i++) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

    void init() {
        state[0] = 0x67452301;
        state[1] = 0xEFCDAB89;
        state[2] = 0x98BADCFE;
        state[3] = 0x10325476;
        state[4] = 0xC3D2E1F0;
        count = 0;
    }

    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buffer[count % 64] = data[i];
            count++;
            if (count % 64 == 0) {
                transform(buffer);
            }
        }
    }

    void finalize(uint8_t digest[20]) {
        uint64_t total_bits = (uint64_t)count * 8;
        size_t idx = count % 64;
        buffer[idx++] = 0x80;
        if (idx > 56) {
            while (idx < 64) buffer[idx++] = 0;
            transform(buffer);
            idx = 0;
        }
        while (idx < 56) buffer[idx++] = 0;
        for (int i = 7; i >= 0; i--) {
            buffer[56 + (7 - i)] = (total_bits >> (i * 8)) & 0xFF;
        }
        transform(buffer);
        for (int i = 0; i < 5; i++) {
            digest[i * 4]     = (state[i] >> 24) & 0xFF;
            digest[i * 4 + 1] = (state[i] >> 16) & 0xFF;
            digest[i * 4 + 2] = (state[i] >> 8) & 0xFF;
            digest[i * 4 + 3] = state[i] & 0xFF;
        }
    }
};

std::string base64_encode(const uint8_t* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (size_t i = 0; i < len; i++) {
        val = (val << 8) + data[i];
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string computeWebSocketAcceptKey(const std::string& clientKey) {
    std::string combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SHA1 sha;
    sha.init();
    sha.update((const uint8_t*)combined.c_str(), combined.length());
    uint8_t digest[20];
    sha.finalize(digest);
    return base64_encode(digest, 20);
}

bool sendWebSocketFrame(int socketFd, const std::string& message) {
    if (socketFd < 0) return false;
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN = 1, opcode = 0x1 (texto UTF-8)
    size_t len = message.size();
    if (len <= 125) {
        frame.push_back((uint8_t)len);
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back((uint8_t)((len >> 8) & 0xFF));
        frame.push_back((uint8_t)(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((uint8_t)((len >> (i * 8)) & 0xFF));
        }
    }
    frame.insert(frame.end(), message.begin(), message.end());

    ssize_t bytesSent = send(socketFd, (const char*)frame.data(), frame.size(), MSG_NOSIGNAL);
    return bytesSent > 0;
}

// ============================================================================
// DISEÑO 1: HILOS INDEPENDIENTES
// Cada vehículo enemigo tiene su propio hilo de ejecución responsable de actualizarlo.
// ============================================================================

struct EnemyCar {
    int id;
    int type;        // 1 a 5 (textura del sprite)
    int laneIndex;   // 0 a 5
    float x;
    float y;
    float speed;
    std::atomic<bool> active;
    std::thread workerThread;

    EnemyCar(int id_, int type_, int lane_, float x_, float y_, float speed_)
        : id(id_), type(type_), laneIndex(lane_), x(x_), y(y_), speed(speed_), active(true) {}
};

// Variables compartidas entre hilos (Memoria Compartida)
std::vector<std::shared_ptr<EnemyCar>> g_activeCars;
std::mutex g_carsMutex;          // Protege la lista de vehículos activos
std::mutex g_socketMutex;        // Protege el socket contra escrituras simultáneas
std::atomic<int> g_nextCarId{1};
std::atomic<bool> g_serverRunning{true};
int g_clientSocket = -1;

/**
 * Función que ejecuta el hilo independiente de cada vehículo enemigo.
 * Cada vehículo posee su propio hilo que actualiza su posición vertical periódicamente.
 */
void carThreadWorker(std::shared_ptr<EnemyCar> car) {
    std::cout << "[DISEÑO 1] Hilo CREADO -> Carro ID: " << car->id
              << " | Carril: " << car->laneIndex 
              << " | Velocidad: " << car->speed
              << " | Thread ID: " << std::this_thread::get_id() << std::endl;

    const int TICK_MS = 20; // Actualización a ~50 Hz
    while (car->active && g_serverRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));

        // Actualización individual de posición
        car->y += car->speed;

        // Comprobar si el vehículo salió de la pantalla inferior
        if (car->y > GAME_HEIGHT + 60.0f) {
            car->active = false;
            break;
        }
    }

    std::cout << "[DISEÑO 1] Hilo FINALIZADO -> Carro ID: " << car->id 
              << " salió de pantalla (Thread ID: " << std::this_thread::get_id() << ")" << std::endl;
}

/**
 * Verifica si un carril está disponible en la zona superior para evitar colisiones
 * entre enemigos al momento del spawn ("no hay colisión entre enemigos").
 */
bool isLaneFreeAtTop(int laneIdx) {
    std::lock_guard<std::mutex> lock(g_carsMutex);
    for (const auto& car : g_activeCars) {
        if (car->active && car->laneIndex == laneIdx) {
            // Si hay un carro activo en este carril y aún no se ha alejado lo suficiente:
            if (car->y < 180.0f) {
                return false;
            }
        }
    }
    return true;
}

/**
 * Hilo generador (Spawner) que crea vehículos y lanza un hilo independiente para cada uno.
 */
void spawnerThreadWorker() {
    std::cout << "[SPAWNER] Hilo generador iniciado." << std::endl;
    int spawnDelayMs = 1200;

    while (g_serverRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(spawnDelayMs));

        // Solo generar carros si hay un cliente conectado
        if (g_clientSocket < 0) continue;

        // Buscar carriles disponibles
        std::vector<int> availableLanes;
        for (int l = 0; l < TOTAL_LANES; l++) {
            if (isLaneFreeAtTop(l)) {
                availableLanes.push_back(l);
            }
        }

        if (availableLanes.empty()) {
            continue; // Ningún carril despejado en la parte superior
        }

        // Seleccionar aleatoriamente un carril libre
        int selectedLane = availableLanes[rand() % availableLanes.size()];
        int carType = (rand() % 5) + 1; // Sprites 1 a 5
        float speed = 2.5f + static_cast<float>(rand() % 25) / 10.0f; // 2.5 a 4.9
        float startX = static_cast<float>(LANE_POSITIONS[selectedLane]);
        float startY = -60.0f;

        int newId = g_nextCarId.fetch_add(1);
        auto newCar = std::make_shared<EnemyCar>(newId, carType, selectedLane, startX, startY, speed);

        {
            // Bloqueo de memoria compartida para registrar el vehículo
            std::lock_guard<std::mutex> lock(g_carsMutex);
            // Iniciar el hilo independiente para este vehículo
            newCar->workerThread = std::thread(carThreadWorker, newCar);
            g_activeCars.push_back(newCar);
        }

        // Ajuste gradual de dificultad
        if (spawnDelayMs > 600) {
            spawnDelayMs -= 5;
        }
    }
}

/**
 * Hilo de transmisión (Broadcaster):
 * Recopila el estado de los vehículos actualizados por sus hilos independientes,
 * limpia los hilos terminados y envía el estado en JSON al frontend vía WebSocket.
 */
void broadcasterThreadWorker() {
    std::cout << "[BROADCASTER] Hilo de transmisión iniciado." << std::endl;

    while (g_serverRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // ~50 FPS

        if (g_clientSocket < 0) continue;

        std::string jsonPayload;
        int activeThreadCount = 0;

        {
            std::lock_guard<std::mutex> lock(g_carsMutex);

            // 1. Limpieza de hilos que hayan finalizado (Reaper de memoria)
            auto it = g_activeCars.begin();
            while (it != g_activeCars.end()) {
                if (!(*it)->active) {
                    if ((*it)->workerThread.joinable()) {
                        (*it)->workerThread.join(); // Esperar terminación limpia del hilo
                    }
                    it = g_activeCars.erase(it);
                } else {
                    ++it;
                }
            }

            activeThreadCount = static_cast<int>(g_activeCars.size());

            // 2. Construcción de mensaje JSON con las posiciones calculadas por cada hilo
            std::ostringstream ss;
            ss << "{\"design\":\"Diseño 1: Hilos Independientes\",\"threads\":" << activeThreadCount << ",\"cars\":[";
            for (size_t i = 0; i < g_activeCars.size(); ++i) {
                const auto& car = g_activeCars[i];
                ss << "{\"id\":" << car->id
                   << ",\"type\":" << car->type
                   << ",\"lane\":" << car->laneIndex
                   << ",\"x\":" << car->x
                   << ",\"y\":" << car->y
                   << ",\"speed\":" << car->speed << "}";
                if (i + 1 < g_activeCars.size()) ss << ",";
            }
            ss << "]}";
            jsonPayload = ss.str();
        }

        // 3. Envío seguro por el socket
        {
            std::lock_guard<std::mutex> sockLock(g_socketMutex);
            if (g_clientSocket >= 0) {
                if (!sendWebSocketFrame(g_clientSocket, jsonPayload)) {
                    std::cout << "[SOCKET] Error al enviar trama. Cliente desconectado." << std::endl;
                    close(g_clientSocket);
                    g_clientSocket = -1;
                }
            }
        }
    }
}

/**
 * Atiende el apretón de manos (Handshake) WebSocket RFC 6455
 */
bool handleWebSocketHandshake(int clientFd) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytesRead = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) return false;

    std::string request(buffer, bytesRead);
    std::string keyHeader = "Sec-WebSocket-Key: ";
    size_t keyPos = request.find(keyHeader);
    if (keyPos == std::string::npos) {
        std::cerr << "[HTTP] Petición no contiene Sec-WebSocket-Key." << std::endl;
        return false;
    }

    size_t keyEnd = request.find("\r\n", keyPos);
    std::string clientKey = request.substr(keyPos + keyHeader.length(), keyEnd - (keyPos + keyHeader.length()));

    std::string acceptKey = computeWebSocketAcceptKey(clientKey);

    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << acceptKey << "\r\n\r\n";

    std::string respStr = response.str();
    send(clientFd, respStr.c_str(), respStr.length(), 0);
    std::cout << "[WEBSOCKET] Handshake completado exitosamente con el cliente." << std::endl;
    return true;
}

// ============================================================================
// Función Principal
// ============================================================================

int main() {
    srand(static_cast<unsigned int>(time(nullptr)));

    std::cout << "=====================================================" << std::endl;
    std::cout << "  MICRO-PROYECTO 1 - PROGRAMACIÓN PARALELA" << std::endl;
    std::cout << "  DISEÑO 1: HILOS INDEPENDIENTES POR VEHÍCULO" << std::endl;
    std::cout << "=====================================================" << std::endl;

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perror("Error al crear socket del servidor");
        return 1;
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Error en bind puerto 5000");
        close(serverFd);
        return 1;
    }

    if (listen(serverFd, 5) < 0) {
        perror("Error en listen");
        close(serverFd);
        return 1;
    }

    std::cout << "[SERVIDOR] Escuchando en puerto " << PORT << "..." << std::endl;

    // Iniciar hilos maestros: generador (spawner) y transmisor (broadcaster)
    std::thread spawnerThread(spawnerThreadWorker);
    std::thread broadcasterThread(broadcasterThreadWorker);

    // Bucle principal de aceptación de clientes
    while (g_serverRunning) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        std::cout << "[SERVIDOR] Esperando conexión del juego (Frontend)..." << std::endl;

        int newClientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);
        if (newClientFd < 0) {
            if (!g_serverRunning) break;
            continue;
        }

        std::cout << "[CONEXIÓN] Cliente conectado desde " << inet_ntoa(clientAddr.sin_addr) << std::endl;

        if (handleWebSocketHandshake(newClientFd)) {
            {
                std::lock_guard<std::mutex> sockLock(g_socketMutex);
                if (g_clientSocket >= 0) {
                    close(g_clientSocket);
                }
                g_clientSocket = newClientFd;
            }

            // Mantener la conexión abierta leyendo tramas del cliente (o detectar desconexión)
            uint8_t inBuffer[1024];
            while (g_serverRunning) {
                ssize_t n = recv(newClientFd, inBuffer, sizeof(inBuffer), 0);
                if (n <= 0) {
                    std::cout << "[CLIENTE] Conexión cerrada por el cliente." << std::endl;
                    break;
                }
                // Si el opcode es 0x8 (Close frame)
                if ((inBuffer[0] & 0x0F) == 0x08) {
                    std::cout << "[CLIENTE] Trama de cierre recibida." << std::endl;
                    break;
                }
            }

            {
                std::lock_guard<std::mutex> sockLock(g_socketMutex);
                if (g_clientSocket == newClientFd) {
                    close(g_clientSocket);
                    g_clientSocket = -1;
                }
            }
        } else {
            close(newClientFd);
        }
    }

    g_serverRunning = false;
    if (spawnerThread.joinable()) spawnerThread.join();
    if (broadcasterThread.joinable()) broadcasterThread.join();
    close(serverFd);

    return 0;
}