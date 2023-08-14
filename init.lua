box.cfg{
    wal_dir = "/tmp/hello-tarantool-c",
    memtx_dir = "/tmp/hello-tarantool-c",
    worker_pool_threads=10,
}

local fiber = require('fiber')
local log = require('log')
local hello = require('hello')
local fio = require('fio')
local errno = require('errno')

local function tick(start, cur, every)
    local dur = tonumber((cur - start))
    return math.fmod(dur, every) == 0
end

local function expired(start, cur, timeout)
    return (cur - start) >= timeout
end

-- NOTE: cartridge.utils.file_write
local function file_write(path, data, opts, perm)
    opts = opts or {'O_CREAT', 'O_WRONLY', 'O_TRUNC'}
    perm = perm or tonumber(644, 8)
    local file = fio.open(path, opts, perm)
    if file == nil then
        return nil, ('%s: %s'):format(path, errno.strerror())
    end

    local res = file:write(data)
    if not res then
        local err = ('%s: %s'):format(path, errno.strerror())
        fio.unlink(path)
        return nil, err
    end

    local res = file:close()
    if not res then
        local err = ('%s: %s'):format(path, errno.strerror())
        fio.unlink(path)
        return nil, err
    end

    return data
end

do
    local c = fiber.cond()
    -- fiber.new(function()
    --     log.info("fiber with cycle without yields")
    --     local start = clock.monotonic64()
    --     while true do
    --         local cur = clock.monotonic64()
    --         if tick(start, cur, 5e6) then
    --             log.info("I am still blocking main thread")
    --         end
    --         if expired(start, cur, 200e6) then
    --             return
    --         end
    --     end
    -- end)

    local files = {}
    local bodies = {}
    for i = 1, 10 do
        table.insert(files, i .. ".txt")
        table.insert(bodies, i .. " " .. i .. " " .. i .. "\n")
    end

    -- fiber.create(function()
    --     log.info('fw start')
    --     for i = 1, 100 do
    --         file_write("tmp/fio/"..files[i], bodies[i])
    --         log.info(i .. ".txt")
    --     end
    --     log.info('fw end')
    -- c:signal()
    -- end)

    for i = 1, 20 do
        fiber.create(function()
            log.info("cw start")
            hello.cw_save("tmp/random_path", files, bodies)
            log.info("cw end")
        end)
    end


    -- for i = 1, 4 do
    --         fiber.create(function()
    --         log.info("cw start")
    --         hello.cw_save("tmp/random_path", files, bodies)
    --         log.info("cw end")
    --     end)
    -- end

    -- fiber.create(function()
    --     log.info("cw start")
    --     hello.cw_save("tmp/coio/random_path", files, bodies)
    --     log.info("cw end")
    -- end)

    -- fiber.create(function()
    --     log.info("cw start")
    --     hello.cw_save("tmp/coio/random_path", files, bodies)
    --     log.info("cw end")
    -- end)

    -- c:wait()
    os.exit()
end
