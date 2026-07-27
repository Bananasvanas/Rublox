#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <lua.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <enet/enet.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <iostream>
#include <sstream>
#include <cstring>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>

// ======================== Компоненты ========================
struct Transform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale    = glm::vec3(1.0f);
};

struct Collider {
    glm::vec3 size;
    bool isStatic = false;
};

struct Mesh {
    unsigned int VAO, VBO, EBO;
    int indexCount;
};

struct Entity {
    int id;
    std::string name;
    Transform transform;
    Collider* collider = nullptr;
    Mesh* mesh = nullptr;
};

// ======================== Глобальные объекты ========================
GLFWwindow* window = nullptr;
std::vector<Entity> entities;
int nextId = 1;
lua_State* L = nullptr;

std::unordered_map<std::string, Mesh> meshCache;

// Сеть
ENetHost* server = nullptr;
ENetHost* client = nullptr;
ENetPeer* serverPeer = nullptr;
bool isServer = false;
bool isClient = false;

// Вещание (broadcast)
std::thread broadcastThread;
std::atomic<bool> broadcasting{false};

// ======================== Lua API ========================
int l_createEntity(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    entities.push_back({nextId, name});
    lua_pushinteger(L, nextId++);
    return 1;
}

int l_moveEntity(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float dx = luaL_checknumber(L, 2);
    float dy = luaL_checknumber(L, 3);
    float dz = luaL_checknumber(L, 4);
    for (auto& e : entities) {
        if (e.id == id) {
            e.transform.position += glm::vec3(dx, dy, dz);
            break;
        }
    }
    return 0;
}

int l_setEntityPosition(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float x = luaL_checknumber(L, 2);
    float y = luaL_checknumber(L, 3);
    float z = luaL_checknumber(L, 4);
    for (auto& e : entities) {
        if (e.id == id) {
            e.transform.position = glm::vec3(x, y, z);
            break;
        }
    }
    return 0;
}

int l_getEntityPosition(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    for (auto& e : entities) {
        if (e.id == id) {
            lua_pushnumber(L, e.transform.position.x);
            lua_pushnumber(L, e.transform.position.y);
            lua_pushnumber(L, e.transform.position.z);
            return 3;
        }
    }
    return 0;
}

int l_isKeyDown(lua_State* L) {
    int key = luaL_checkinteger(L, 1);
    lua_pushboolean(L, glfwGetKey(window, key) == GLFW_PRESS);
    return 1;
}

int l_addCollider(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float sx = luaL_checknumber(L, 2);
    float sy = luaL_checknumber(L, 3);
    float sz = luaL_checknumber(L, 4);
    bool isStatic = lua_toboolean(L, 5);
    for (auto& e : entities) {
        if (e.id == id) {
            e.collider = new Collider{{sx, sy, sz}, isStatic};
            break;
        }
    }
    return 0;
}

int l_loadModel(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (meshCache.find(path) == meshCache.end()) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate | aiProcess_FlipUVs);
        if (!scene || !scene->mMeshes[0]) {
            lua_pushboolean(L, false);
            return 1;
        }
        aiMesh* aimesh = scene->mMeshes[0];
        std::vector<float> verts;
        std::vector<unsigned int> indices;
        for (unsigned int i = 0; i < aimesh->mNumVertices; i++) {
            verts.push_back(aimesh->mVertices[i].x);
            verts.push_back(aimesh->mVertices[i].y);
            verts.push_back(aimesh->mVertices[i].z);
        }
        for (unsigned int i = 0; i < aimesh->mNumFaces; i++) {
            aiFace face = aimesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }
        Mesh mesh;
        glGenVertexArrays(1, &mesh.VAO);
        glGenBuffers(1, &mesh.VBO);
        glGenBuffers(1, &mesh.EBO);
        glBindVertexArray(mesh.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size()*sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        mesh.indexCount = indices.size();
        meshCache[path] = mesh;
    }
    lua_pushboolean(L, true);
    return 1;
}

int l_setEntityMesh(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* path = luaL_checkstring(L, 2);
    auto it = meshCache.find(path);
    if (it != meshCache.end()) {
        for (auto& e : entities) {
            if (e.id == id) {
                e.mesh = &it->second;
                break;
            }
        }
    }
    return 0;
}

// ======================== Сетевые Lua-функции ========================
int l_createServer(lua_State* L) {
    int port = luaL_checkinteger(L, 1);
    if (server) { lua_pushboolean(L, false); return 1; }
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    server = enet_host_create(&addr, 32, 2, 0, 0);
    if (!server) { lua_pushboolean(L, false); return 1; }
    isServer = true;
    lua_pushboolean(L, 1); lua_setglobal(L, "isServer");  // чтобы Lua знал
    lua_pushboolean(L, true);
    return 1;
}

int l_connectToServer(lua_State* L) {
    const char* ip = luaL_checkstring(L, 1);
    int port = luaL_checkinteger(L, 2);
    if (client) { lua_pushboolean(L, false); return 1; }
    client = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!client) { lua_pushboolean(L, false); return 1; }
    ENetAddress addr;
    enet_address_set_host(&addr, ip);
    addr.port = port;
    serverPeer = enet_host_connect(client, &addr, 2, 0);
    if (!serverPeer) { lua_pushboolean(L, false); return 1; }
    isClient = true;
    lua_pushboolean(L, 1); lua_setglobal(L, "isClient");  // для Lua
    lua_pushboolean(L, true);
    return 1;
}

int l_sendData(lua_State* L) {
    const char* data = luaL_checkstring(L, 1);
    if (isServer && server) {
        ENetPacket* packet = enet_packet_create(data, strlen(data)+1, ENET_PACKET_FLAG_RELIABLE);
        enet_host_broadcast(server, 0, packet);
    } else if (isClient && serverPeer) {
        ENetPacket* packet = enet_packet_create(data, strlen(data)+1, ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(serverPeer, 0, packet);
    }
    return 0;
}

int l_getNetworkEvent(lua_State* L) {
    ENetEvent event;
    int hasEvent = 0;
    if (isServer) hasEvent = enet_host_service(server, &event, 0);
    else if (isClient) hasEvent = enet_host_service(client, &event, 0);

    if (hasEvent > 0) {
        lua_newtable(L);
        lua_pushstring(L, "type");
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                lua_pushstring(L, "connect"); break;
            case ENET_EVENT_TYPE_DISCONNECT:
                lua_pushstring(L, "disconnect"); break;
            case ENET_EVENT_TYPE_RECEIVE:
                lua_pushstring(L, "data");
                lua_pushstring(L, "data");
                lua_pushstring(L, (const char*)event.packet->data);
                lua_settable(L, -3);
                enet_packet_destroy(event.packet);
                break;
            default: lua_pushstring(L, "none");
        }
        lua_settable(L, -3);
        return 1;
    }
    return 0;
}

// ======================== Broadcast (UDP) ========================
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

void broadcastLoop(int port, const std::string& gameName) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcastEnable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcastEnable, sizeof(broadcastEnable));
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_BROADCAST;

    std::string msg = "RUBLOX|" + gameName + "|7777";
    while (broadcasting) {
        sendto(sock, msg.c_str(), msg.size(), 0, (sockaddr*)&addr, sizeof(addr));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    closesocket(sock);
}

int l_startBroadcast(lua_State* L) {
    int port = luaL_checkinteger(L, 1);
    const char* gameName = luaL_checkstring(L, 2);
    if (broadcasting) { lua_pushboolean(L, false); return 1; }
    broadcasting = true;
    broadcastThread = std::thread(broadcastLoop, port, gameName);
    lua_pushboolean(L, true);
    return 1;
}

int l_stopBroadcast(lua_State* L) {
    broadcasting = false;
    if (broadcastThread.joinable()) broadcastThread.join();
    return 0;
}

int l_discoverServers(lua_State* L) {
    int port = luaL_checkinteger(L, 1);
    float timeout = luaL_optnumber(L, 2, 3.0);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (sockaddr*)&addr, sizeof(addr));

    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    auto start = std::chrono::steady_clock::now();
    lua_newtable(L);
    int index = 1;

    char buf[256];
    while (std::chrono::steady_clock::now() - start < std::chrono::duration<float>(timeout)) {
        sockaddr_in from;
        int fromLen = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromLen);
        if (len > 0) {
            buf[len] = '\0';
            std::string data(buf);
            if (data.rfind("RUBLOX|", 0) == 0) {
                size_t pos1 = data.find('|');
                size_t pos2 = data.find('|', pos1+1);
                if (pos1 != std::string::npos && pos2 != std::string::npos) {
                    std::string gameName = data.substr(pos1+1, pos2-pos1-1);
                    int serverPort = std::stoi(data.substr(pos2+1));
                    char ip[32];
                    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                    lua_newtable(L);
                    lua_pushstring(L, "ip"); lua_pushstring(L, ip); lua_settable(L, -3);
                    lua_pushstring(L, "game"); lua_pushstring(L, gameName.c_str()); lua_settable(L, -3);
                    lua_pushstring(L, "port"); lua_pushinteger(L, serverPort); lua_settable(L, -3);
                    lua_rawseti(L, -2, index++);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    closesocket(sock);
    return 1;
}

int l_listGames(lua_State* L) {
    lua_newtable(L);
    int idx = 1;
    for (auto& entry : std::filesystem::directory_iterator("Games")) {
        if (entry.is_directory()) {
            lua_pushstring(L, entry.path().filename().string().c_str());
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

// ======================== Физика ========================
void checkCollisions() {
    for (size_t i = 0; i < entities.size(); ++i) {
        if (!entities[i].collider) continue;
        for (size_t j = i+1; j < entities.size(); ++j) {
            if (!entities[j].collider) continue;
            Entity& a = entities[i];
            Entity& b = entities[j];
            glm::vec3 aMin = a.transform.position - a.collider->size * 0.5f;
            glm::vec3 aMax = a.transform.position + a.collider->size * 0.5f;
            glm::vec3 bMin = b.transform.position - b.collider->size * 0.5f;
            glm::vec3 bMax = b.transform.position + b.collider->size * 0.5f;
            bool overlap = (aMin.x < bMax.x && aMax.x > bMin.x &&
                            aMin.y < bMax.y && aMax.y > bMin.y &&
                            aMin.z < bMax.z && aMax.z > bMin.z);
            if (overlap) {
                if (!a.collider->isStatic && !b.collider->isStatic) {
                    glm::vec3 pushDir = a.transform.position - b.transform.position;
                    float len = glm::length(pushDir);
                    if (len > 0.0001f) {
                        pushDir /= len;
                        float overlapDist = (glm::length(a.collider->size) + glm::length(b.collider->size))*0.5f - len;
                        if (overlapDist > 0) {
                            a.transform.position += pushDir * overlapDist * 0.5f;
                            b.transform.position -= pushDir * overlapDist * 0.5f;
                        }
                    }
                }
                lua_getglobal(L, "onCollision");
                if (lua_isfunction(L, -1)) {
                    lua_pushinteger(L, a.id);
                    lua_pushinteger(L, b.id);
                    lua_pcall(L, 2, 0, 0);
                } else {
                    lua_pop(L, 1);
                }
            }
        }
    }
}

// ======================== Рендеринг ========================
unsigned int shader;
void setupShader() {
    const char* vs = R"(
    #version 330 core
    layout(location=0) in vec3 aPos;
    uniform mat4 model, view, proj;
    void main(){ gl_Position = proj * view * model * vec4(aPos,1.0); }
    )";
    const char* fs = R"(
    #version 330 core
    out vec4 color;
    uniform vec3 objColor;
    void main(){ color = vec4(objColor, 1.0); }
    )";
    auto compile = [](const char* src, unsigned int type) {
        unsigned int s = glCreateShader(type);
        glShaderSource(s, 1, &src, NULL); glCompileShader(s);
        return s;
    };
    unsigned int v = compile(vs, GL_VERTEX_SHADER);
    unsigned int f = compile(fs, GL_FRAGMENT_SHADER);
    shader = glCreateProgram();
    glAttachShader(shader, v); glAttachShader(shader, f); glLinkProgram(shader);
    glDeleteShader(v); glDeleteShader(f);
}

void render() {
    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(shader);
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -10));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 800.0f/600.0f, 0.1f, 100.0f);
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "proj"), 1, GL_FALSE, glm::value_ptr(proj));

    for (auto& e : entities) {
        if (!e.mesh) continue;
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, e.transform.position);
        model = glm::rotate(model, e.transform.rotation.x, glm::vec3(1,0,0));
        model = glm::rotate(model, e.transform.rotation.y, glm::vec3(0,1,0));
        model = glm::rotate(model, e.transform.rotation.z, glm::vec3(0,0,1));
        model = glm::scale(model, e.transform.scale);
        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3f(glGetUniformLocation(shader, "objColor"), 0.8f, 0.3f, 0.3f);
        glBindVertexArray(e.mesh->VAO);
        glDrawElements(GL_TRIANGLES, e.mesh->indexCount, GL_UNSIGNED_INT, 0);
    }
}

// ======================== Главная функция ========================
int main() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(800, 600, "Rublox", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1);
    glEnable(GL_DEPTH_TEST);
    setupShader();

    enet_initialize();

    L = luaL_newstate();
    luaL_openlibs(L);
    lua_register(L, "createEntity", l_createEntity);
    lua_register(L, "moveEntity", l_moveEntity);
    lua_register(L, "setEntityPosition", l_setEntityPosition);
    lua_register(L, "getEntityPosition", l_getEntityPosition);
    lua_register(L, "isKeyDown", l_isKeyDown);
    lua_register(L, "addCollider", l_addCollider);
    lua_register(L, "loadModel", l_loadModel);
    lua_register(L, "setEntityMesh", l_setEntityMesh);
    lua_register(L, "createServer", l_createServer);
    lua_register(L, "connectToServer", l_connectToServer);
    lua_register(L, "sendData", l_sendData);
    lua_register(L, "getNetworkEvent", l_getNetworkEvent);
    lua_register(L, "startBroadcast", l_startBroadcast);
    lua_register(L, "stopBroadcast", l_stopBroadcast);
    lua_register(L, "discoverServers", l_discoverServers);
    lua_register(L, "listGames", l_listGames);

    if (luaL_dofile(L, "menu.lua") != LUA_OK) {
        std::cerr << "Lua error: " << lua_tostring(L, -1) << std::endl;
    }

    float lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        float dt = glfwGetTime() - lastTime;
        lastTime += dt;

        lua_getglobal(L, "update");
        if (lua_isfunction(L, -1)) {
            lua_pushnumber(L, dt);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                std::cerr << "Lua error: " << lua_tostring(L, -1) << std::endl;
        } else lua_pop(L, 1);

        checkCollisions();
        render();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    if (server) enet_host_destroy(server);
    if (client) enet_host_destroy(client);
    enet_deinitialize();
    broadcasting = false;
    if (broadcastThread.joinable()) broadcastThread.join();
    glfwTerminate();
    WSACleanup();
    return 0;
}