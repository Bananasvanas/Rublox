-- Rublox Launcher
print("=== Rublox Launcher ===")
local games = listGames()
if #games == 0 then
    print("No games found in Games/ folder.")
    os.exit()
end

print("\nInstalled games:")
for i, name in ipairs(games) do
    print(i .. ". " .. name)
end

print("\n1. Join LAN Server")
print("2. Create Server")
print("3. Exit")
io.write("Choice: ")
local choice = io.read("*n")

if choice == 1 then
    print("Searching for servers...")
    local servers = discoverServers(7778, 3)
    if #servers == 0 then
        print("No servers found.")
        os.exit()
    end
    print("\nFound servers:")
    for i, s in ipairs(servers) do
        print(i .. ". " .. s.ip .. " - " .. s.game)
    end
    io.write("Choose server number: ")
    local idx = io.read("*n")
    if servers[idx] then
        local s = servers[idx]
        local gamePath = "Games/" .. s.game .. "/game.lua"
        if dofile(gamePath) then
            connectToServer(s.ip, s.port)
            print("Connected to " .. s.ip)
        else
            print("Failed to load game: " .. gamePath)
            os.exit()
        end
    end
elseif choice == 2 then
    io.write("Choose game number to host: ")
    local gidx = io.read("*n")
    if games[gidx] then
        local gameName = games[gidx]
        local gamePath = "Games/" .. gameName .. "/game.lua"
        if dofile(gamePath) then
            createServer(7777)
            startBroadcast(7778, gameName)
            print("Server created for " .. gameName)
        else
            print("Failed to load game: " .. gamePath)
            os.exit()
        end
    end
else
    os.exit()
end