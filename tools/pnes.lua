-- Mesen script for pnes log points. Loads logdata.lua's { lma, message }
-- entries -- LMA being the flat, bank-agnostic PRG-ROM offset of a log point --
-- and fires the message when the CPU executes the instruction sitting there.
--
-- log() call sites cost zero ROM bytes and zero cycles on hardware; this script
-- is the only place they have any effect at all. Needs Mesen's "Allow access to
-- I/O and OS functions" option, which dofile requires.

-- Keyed by LMA, and each value is a LIST: log() and pause() cost zero real
-- instructions, so two adjacent call sites routinely resolve to the same
-- "nearest real instruction" address. One entry per LMA would let whichever
-- loaded last silently hide the others.

local logByLma = {}

local memoryLayout = nil

-- Returns a `reset` function, so the overlay can be zeroed without reloading.

local function startStackTracking(stackTop, stackBottom, silentStackAmount)
    local totalBytes = stackTop - stackBottom + 1
    local maxDepth = 0

    local function onStackPointerWrite()
        local sp = emu.read16(0, emu.memType.nesMemory, false)
        if sp > stackTop or sp < stackBottom then return end

        local depth = stackTop - sp
        if depth > maxDepth then
            maxDepth = depth
            if maxDepth > silentStackAmount then
                local pct = math.floor(100 * maxDepth / totalBytes)
                emu.log(string.format("cstack: new high-water mark -- %d/%d bytes (%d%%)",
                    maxDepth, totalBytes, pct))
            end
        end
    end

    emu.addMemoryCallback(onStackPointerWrite, emu.callbackType.write,
        0, 1, emu.cpuType.nes, emu.memType.nesMemory)

    return function() maxDepth = 0 end
end

-- Named, contiguous regions of prg_ram, built once in startMemoryTracking, so a
-- stray value can be reported as the region it wandered into.
local function hunkNameFor(hunks, address)
    for _, hunk in ipairs(hunks) do
        if address <= hunk.upTo and (not hunk.start or address >= hunk.start) then
            return hunk.name
        end
    end
    return "unmapped memory"
end

local function startStackOverflowGuard(hunks, stackTop, stackBottom)
    local violated = false

    local function checkOnce()
        local sp = emu.read16(0, emu.memType.nesMemory, false)
        local isViolation = sp < stackBottom or sp > stackTop
        if isViolation and not violated then
            violated = true
            emu.log(string.format(
                "app: STACK OVERFLOW -- stack pointer settled at 0x%x, outside its reserved "
                .. "region [0x%x, 0x%x] -- now inside the %s region",
                sp, stackBottom, stackTop, hunkNameFor(hunks, sp)))
        elseif violated and not isViolation then
            violated = false
            emu.log(string.format("app: stack back within its reserved region (SP = 0x%x)", sp))
        end
    end

    emu.addEventCallback(checkOnce, emu.eventType.startFrame)

    return function() violated = false end
end

-- Reserved-memory-overwrite guard: catches the stack settling outside its own
-- region, in either direction -- link.ld pins __stack = __heap_start - 1, so
-- stack and heap sit back to back with no gap.
--
-- Deliberately not the per-write callback the high-water tracker uses: $00/$01
-- is two independent 8-bit writes, so a multi-byte SP update is often visible
-- mid-update here. Sampling once per settled frame sidesteps that rather than
-- trying to debounce it.
--
-- It does not attempt the reverse (a heap write into the stack hunk): an
-- ordinary heap write carries no signal that tells it apart from a normal stack
-- write to the same address.
local BOUNDARY_TAG_BYTES = 2 -- at EACH end (header, footer)

local HEAP_GRANULE = 8

local function heapCellTouched(touched, start, stop)
    for i = start, stop do
        if touched[i] then return true end
    end
    return false
end

-- One boolean per reserved byte, set the first time that byte is written. A bit
-- staying set after free() reuses the byte is intentional: this tracks the
-- cumulative footprint ever touched, which is the real answer to "how big does
-- the reservation need to be".
--
-- BOUNDARY_TAG_BYTES excludes malloc's own bookkeeping from both ends --
-- confirmed by disassembling a real build: a 2-byte header at the start of the
-- initial free chunk and a matching footer at its end. Only exact for that
-- initial state, since every free chunk gets its own pair once the heap
-- fragments; it then undercounts overhead rather than overcounting.
local function heapRunLengthEncoding(touched, heapSize)
    local parts = {}
    local usedRuns, usedQwords = 0, 0
    local runIsUsed, runLen = nil, 0
    local i = 1
    while i <= heapSize do
        local cellEnd = math.min(i + HEAP_GRANULE - 1, heapSize)
        local isUsed = heapCellTouched(touched, i, cellEnd)
        if isUsed then usedQwords = usedQwords + 1 end
        if runIsUsed == nil then
            runIsUsed, runLen = isUsed, 1
        elseif isUsed == runIsUsed then
            runLen = runLen + 1
        else
            if runIsUsed then usedRuns = usedRuns + 1 end
            parts[#parts + 1] = string.format("%s(%d)", runIsUsed and "u" or "e", runLen)
            runIsUsed, runLen = isUsed, 1
        end
        i = cellEnd + 1
    end
    if runIsUsed ~= nil then
        if runIsUsed then usedRuns = usedRuns + 1 end
        parts[#parts + 1] = string.format("%s(%d)", runIsUsed and "u" or "e", runLen)
    end
    return table.concat(parts, " "), usedRuns, usedQwords
end

local function startHeapTracking(heapStart, heapSize, silentHeapAmount)
    local usableStart = heapStart + BOUNDARY_TAG_BYTES
    local usableSize = heapSize - 2 * BOUNDARY_TAG_BYTES
    if usableSize <= 0 then
        usableStart = heapStart
        usableSize = heapSize
    end

    local totalQwords = math.ceil(usableSize / HEAP_GRANULE)
    local silentQwords = math.ceil(silentHeapAmount / HEAP_GRANULE)

    local touched = {}
    local lastReportedQwords = 0

    local function onHeapWrite(address)
        local offset = address - usableStart + 1
        if touched[offset] then return end
        touched[offset] = true

        local pattern, usedRuns, usedQwords = heapRunLengthEncoding(touched, usableSize)
        if usedQwords ~= lastReportedQwords then
            lastReportedQwords = usedQwords
            if usedQwords > silentQwords then
                local pct = math.floor(100 * usedQwords / totalQwords)
                emu.log(string.format("app: heap usage (%d/%d qwords) %d%%",
                    usedQwords, totalQwords, pct))
                if usedRuns > 1 then
                    emu.log("app: heap frag " .. pattern)
                end
            end

            if usedQwords == totalQwords then
                emu.log("app: heap exhausted -- every usable qword is now committed to some "
                    .. "chunk; grow __heap_default_limit (see demo/link.ld) or reduce allocations.")
            end
        end
    end

    emu.addMemoryCallback(onHeapWrite, emu.callbackType.write,
        usableStart, usableStart + usableSize - 1, emu.cpuType.nes, emu.memType.nesMemory)

    return function()
        touched = {}
        lastReportedQwords = 0
    end
end

local function buildHunks(memory)
    return {
        { name = "static (.data/.bss/.noinit)", upTo = memory.stackBottom - 1 },
        { name = "stack", start = memory.stackBottom, upTo = memory.stackTop },
        { name = "heap", start = memory.heapStart, upTo = memory.heapStart + memory.heapSize - 1 },
    }
end

local function startMemoryTracking(memory)
    emu.log(string.format("app: memory tracking active -- stack [0x%x, 0x%x] (%d bytes), heap [0x%x, 0x%x] (%d bytes)",
        memory.stackBottom, memory.stackTop, memory.stackTop - memory.stackBottom + 1,
        memory.heapStart, memory.heapStart + memory.heapSize - 1, memory.heapSize))
    local hunks = buildHunks(memory)
    local stackTotalBytes = memory.stackTop - memory.stackBottom + 1
    local resetStack = startStackTracking(memory.stackTop, memory.stackBottom,
        memory.silentStackAmount or stackTotalBytes)
    local resetOverflowGuard = startStackOverflowGuard(hunks, memory.stackTop, memory.stackBottom)
    local resetHeap = startHeapTracking(memory.heapStart, memory.heapSize,
        memory.silentHeapAmount or memory.heapSize)
    return function()
        resetStack()
        resetOverflowGuard()
        resetHeap()
    end
end

local RETRY_INTERVAL_FRAMES = 60
local framesUntilRetry = 0
local retryCallbackRef = nil

local function logDataPath()
    local romPath = emu.getRomInfo().path
    local romDir = romPath:match("^(.*[/\\])") or ""
    return romDir .. "logdata.lua"
end

local function tryLoadLogData()
    local ok, root = pcall(dofile, logDataPath())
    if not ok then
        return false, root -- root is the pcall error message here
    end
    if type(root) ~= "table" or type(root.logs) ~= "table" then
        return false, "logdata.lua did not return { logs = {...} }"
    end
    for _, entry in ipairs(root.logs) do
        local bucket = logByLma[entry.lma]
        if not bucket then
            bucket = {}
            logByLma[entry.lma] = bucket
        end
        table.insert(bucket, entry)
    end
    for _, bucket in pairs(logByLma) do
        table.sort(bucket, function(a, b) return a.line < b.line end)
    end
    emu.log("app: found " .. logDataPath() .. " -- " .. #root.logs .. " log point(s) active.")

    local resetMemoryTracking = nil
    if root.memory then
        memoryLayout = root.memory
        resetMemoryTracking = startMemoryTracking(memoryLayout)
    else
        emu.log("app: logdata.lua has no `memory` table -- stack/heap tracking disabled for this build.")
    end

    local resetRef
    resetRef = emu.addEventCallback(function()
        emu.removeEventCallback(resetRef, emu.eventType.startFrame)
        if resetMemoryTracking then resetMemoryTracking() end
        emu.reset()
    end, emu.eventType.startFrame)

    return true
end

local function onStartFrame()
    framesUntilRetry = framesUntilRetry - 1
    if framesUntilRetry > 0 then return end
    framesUntilRetry = RETRY_INTERVAL_FRAMES

    local loaded, err = tryLoadLogData()
    if loaded then
        emu.removeEventCallback(retryCallbackRef, emu.eventType.startFrame)
        retryCallbackRef = nil
    else
        emu.log("app: still can't load " .. logDataPath() .. " -- " .. tostring(err)
            .. " (build the NES target first, or enable \"Allow access to I/O "
            .. "and OS functions\" for this script if that's what's blocking it)")
    end
end

local loaded, err = tryLoadLogData()
if not loaded then
    emu.log("app: " .. logDataPath() .. " not found -- " .. tostring(err)
        .. ". It should exist before the script runs (produced by the NES build); "
        .. "will keep retrying.")
    retryCallbackRef = emu.addEventCallback(onStartFrame, emu.eventType.startFrame)
end

local WINDOW_BASE  = 0x6000
local WINDOW_COUNT = 5
local WINDOW_SIZE  = 0x2000
local WINDOW_END   = WINDOW_BASE + WINDOW_COUNT * WINDOW_SIZE - 1

local windowLmaBase = {}

local function refreshWindowMap()
    for i = 0, WINDOW_COUNT - 1 do
        local cpuAddr = WINDOW_BASE + i * WINDOW_SIZE
        local mapped = emu.convertAddress(cpuAddr, emu.memType.nesMemory, emu.cpuType.nes)
        if mapped and mapped.memType == emu.memType.nesPrgRom then
            windowLmaBase[i] = mapped.address
        else
            windowLmaBase[i] = nil
        end
    end
end

refreshWindowMap()

local function readArgValue(arg)
    if arg.size == 1 then return emu.read(arg.address, emu.memType.nesMemory, arg.signed)
    elseif arg.size == 2 then return emu.read16(arg.address, emu.memType.nesMemory, arg.signed)
    else return emu.read32(arg.address, emu.memType.nesMemory, arg.signed)
    end
end

local function formatMessage(message, args)
    if not args or #args == 0 then return message end
    local i = 0
    return (message:gsub("%%[duxX]", function(spec)
        i = i + 1
        local arg = args[i]
        if not arg then return spec end
        local value = readArgValue(arg)
        if spec == "%x" then return string.format("%x", value) end
        if spec == "%X" then return string.format("%X", value) end
        return tostring(value)
    end))
end

local function onExec(address)
    local index  = math.floor((address - WINDOW_BASE) / WINDOW_SIZE)
    local base   = windowLmaBase[index]
    if not base then return end

    local lma = base + ((address - WINDOW_BASE) % WINDOW_SIZE)
    local bucket = logByLma[lma]
    if not bucket then return end

    local shouldBreak = false
    for _, entry in ipairs(bucket) do
        if entry.kind == "pause" then
            emu.log("app: pause() hit at " .. entry.file .. ":" .. entry.line)
            shouldBreak = true
        else
            local message = formatMessage(entry.message, entry.args)
            emu.log("log: " .. message)
            emu.displayMessage("log", message)
        end
    end
    if shouldBreak then emu.breakExecution() end
end

emu.addMemoryCallback(refreshWindowMap, emu.callbackType.write,
    WINDOW_BASE, WINDOW_END, emu.cpuType.nes, emu.memType.nesMemory)

emu.addMemoryCallback(onExec, emu.callbackType.exec,
    WINDOW_BASE, WINDOW_END, emu.cpuType.nes, emu.memType.nesMemory)
