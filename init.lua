box.cfg{
    wal_dir = "/tmp/hello-tarantool-c",
    memtx_dir = "/tmp/hello-tarantool-c",
}

local fiber = require('fiber')
local clock = require('clock')
local log = require('log')
local hello = require('hello')

local function tick(start, cur, every)
    local dur = tonumber((cur - start))
    return math.fmod(dur, every) == 0
end

local function expired(start, cur, timeout)
    return (cur - start) >= timeout
end

do
    fiber.new(function()
        log.info("fiber with cycle without yields")
        local start = clock.monotonic64()
        while true do
            local cur = clock.monotonic64()
            if tick(start, cur, 5e6) then
                log.info("I am still blocking main thread")
            end
            if expired(start, cur, 200e6) then
                return
            end
        end
    end)

    -- fiber.create(function()
        -- log.info("fiber.create with coio call for debug")
        -- hello.run('./tmp_dir')
        -- log.info("coio call finished")
    -- end)

    fiber.create(function()
        log.info("fiber.create with coio call mktree")
        hello.mktree({"debug", "directory", "gygyk"})
        log.info("coio call finished")
    end)

    os.exit()
end
