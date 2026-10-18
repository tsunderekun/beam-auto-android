-- lua/ge/extensions/android/audio.lua
-- 3D Spatial Audio & Native Vehicle Interactive Trigger Bridge for Android Screen Mirroring

local M = {}

local mime = rawget(_G, 'mime') or require('mime')
local socketOk, socket = pcall(require, 'socket')
if not socketOk then socket = rawget(_G, 'socket') end

local HOST = "127.0.0.1"
local AUDIO_PORT = 8767
local HTTP_PORT = 8766

local sock = nil
local connected = false
local reconnectTimer = 0
local posTimer = 0
local READ_SIZE = 8192
local jsTimer = 0
local audioRemainder = ""
local audioProtocolBuffer = ""
local audioSampleRate = 48000
local audioChannels = 2
local audioBits = 16
local audioFormatReady = false

local androidAudioJS = [==[
(function(){
  if (window._androidAudio && window._androidAudio._ready) return;
  var AC = window.AudioContext || window.webkitAudioContext;
  if (!AC) return;

  var ctx = null;
  var panner = null;
  var filter = null;
  var nextPlayTime = 0;
  var banner = null;

  var OUT_HZ = 6000;
  var MUFFLE_NEAR = 1.0;
  var MUFFLE_FAR = 6.0;
  var MUFFLE_HZ = 900;
  var OPEN_HZ = 20000;

  function showBanner(){
    if (banner || (ctx && ctx.state === 'running')) return;
    try {
      banner = document.createElement('div');
      banner.id = 'android-audio-banner';
      banner.innerHTML = '🔊 Нажмите здесь для включения 3D звука Android';
      banner.style.cssText = 'position:fixed;top:16px;left:50%;transform:translateX(-50%);background:#181a20;color:#e2e6ee;padding:9px 22px;border-radius:24px;font-family:sans-serif;font-size:13px;font-weight:600;z-index:999999;cursor:pointer;border:1px solid #383c48;box-shadow:0 6px 18px rgba(0,0,0,0.6);transition:all 0.2s ease;';
      banner.onmouseover = function(){ banner.style.background = '#252934'; };
      banner.onmouseout = function(){ banner.style.background = '#181a20'; };
      banner.onclick = function(e){
        e.stopPropagation();
        unlock();
      };
      document.body.appendChild(banner);
    } catch(e){}
  }

  function hideBanner(){
    if (banner) {
      try { banner.remove(); } catch(e){}
      banner = null;
    }
  }

  function initGraph(){
    if (ctx) return true;
    try {
      ctx = new AC({sampleRate: 48000});
      filter = ctx.createBiquadFilter();
      filter.type = 'lowpass';
      filter.frequency.value = OPEN_HZ;

      panner = ctx.createPanner();
      panner.panningModel = 'equalpower';
      panner.distanceModel = 'inverse';
      panner.refDistance = 2.0;
      panner.maxDistance = 400.0;
      panner.rolloffFactor = 1.0;

      filter.connect(panner);
      panner.connect(ctx.destination);
      return true;
    } catch(e) {
      console.error('Android Audio graph initialization failed:', e);
      return false;
    }
  }

  function unlock(){
    initGraph();
    if (!ctx) return;
    if (ctx.state === 'suspended') {
      ctx.resume().then(function(){
        hideBanner();
        if (window.bngApi) window.bngApi.engineLua("log('I','android_audio','AudioContext unlocked & running')");
      }).catch(function(){});
    } else if (ctx.state === 'running') {
      hideBanner();
    }
  }

  ['pointerdown', 'mousedown', 'click', 'keydown'].forEach(function(ev){
    window.addEventListener(ev, unlock, {capture: true});
    document.addEventListener(ev, unlock, {capture: true});
  });

  window._androidAudio = {
    _ready: true,
    resume: function(){
      unlock();
    },
     push: function(b64, sampleRate, channels, bits){
       if (!initGraph()) return;
      if (ctx.state === 'suspended') {
        showBanner();
        return;
      }
      hideBanner();

       var bin = atob(b64);
       var hz = Number(sampleRate) || 48000;
       var ch = Math.max(1, Math.min(8, Number(channels) || 2));
       var bytesPerSample = Math.max(1, (Number(bits) || 16) / 8);
       var frameBytes = ch * bytesPerSample;
       var numFrames = Math.floor(bin.length / frameBytes);
       if (numFrames <= 0 || bytesPerSample !== 2) return;

       var buf = ctx.createBuffer(ch, numFrames, hz);

       var idx = 0;
       for (var i = 0; i < numFrames; i++) {
         for (var c = 0; c < ch; c++) {
           var sample = bin.charCodeAt(idx) | (bin.charCodeAt(idx + 1) << 8);
           if (sample >= 32768) sample -= 65536;
           buf.getChannelData(c)[i] = sample / 32768.0;
           idx += 2;
         }
      }

      var src = ctx.createBufferSource();
      src.buffer = buf;
      src.connect(filter);

      var now = ctx.currentTime;
      if (nextPlayTime < now || nextPlayTime > now + 0.05) {
        nextPlayTime = now + 0.015; // 15ms ultra low-latency jitter buffer
      }
      src.start(nextPlayTime);
      nextPlayTime += buf.duration;
    },

    pos: function(x, y, z, inside, md){
      if (!panner || !filter || !ctx) return;
      if (panner.positionX) {
        panner.positionX.value = x;
        panner.positionY.value = y;
        panner.positionZ.value = z;
      } else {
        panner.setPosition(x, y, z);
      }

      var f = OPEN_HZ;
      if (!inside) {
        var dm = (md !== undefined) ? md : Math.sqrt(x * x + y * y + z * z);
        var t = (dm - MUFFLE_NEAR) / (MUFFLE_FAR - MUFFLE_NEAR);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        f = OUT_HZ * Math.pow(MUFFLE_HZ / OUT_HZ, t);
      }
      filter.frequency.setTargetAtTime(f, ctx.currentTime, 0.08);
    }
  };
})();
]==]

local function ensureJS()
  be:queueJS(androidAudioJS)
end

local function sendHttpCommand(path)
  if not socket or not socket.tcp then return false end
  local c = socket.tcp()
  c:settimeout(0.2)
  local ok, err = c:connect(HOST, HTTP_PORT)
  if ok then
    c:send("GET " .. path .. " HTTP/1.0\r\nHost: " .. HOST .. "\r\nConnection: close\r\n\r\n")
    pcall(function() c:receive("*l") end)
    pcall(function() c:close() end)
  end
end

local function connectAudio()
  if not socket or not socket.tcp then
    connected = false
    return false
  end
  if sock then
    pcall(function() sock:close() end)
    sock = nil
  end
  local c = socket.tcp()
  c:settimeout(0.05)
  local ok, err = c:connect(HOST, AUDIO_PORT)
  if ok then
    c:settimeout(0) -- Non-blocking
    sock = c
    connected = true
    audioProtocolBuffer = ""
    audioRemainder = ""
    audioFormatReady = false
    log('I', 'android_audio', 'Connected to Android 3D Audio stream on port ' .. AUDIO_PORT)
    ensureJS()
    return true
  else
    pcall(function() c:close() end)
    connected = false
    sock = nil
    return false
  end
end

local function proj(a, b)
  return a.x * b.x + a.y * b.y + a.z * b.z
end

local function updatePosition(dt)
  posTimer = posTimer + dt
  if posTimer < 0.1 then return end
  posTimer = 0

  if not (core_camera and core_camera.getPosition and core_camera.getForward and core_camera.getUp) then
    return
  end

  local veh = be:getPlayerVehicle(0)
  if not veh then return end

  local cam = core_camera.getPosition()
  local fwd = core_camera.getForward()
  local up = core_camera.getUp()
  if not (cam and fwd and up) then return end

  local pos = veh:getPosition()
  local d = {x = pos.x - cam.x, y = pos.y - cam.y, z = pos.z - cam.z}
  local right = {
    x = fwd.y * up.z - fwd.z * up.y,
    y = fwd.z * up.x - fwd.x * up.z,
    z = fwd.x * up.y - fwd.y * up.x,
  }

  local x = proj(d, right)
  local y = proj(d, up)
  local z = -proj(d, fwd)

  local inside = false
  if core_camera.isCameraInside then
    inside = (core_camera.isCameraInside(0, cam) == 1)
  elseif core_camera.getActiveCamName then
    inside = (core_camera.getActiveCamName(0) == 'driver')
  end

  -- In cabin, center stereo sound directly in front of driver
  if inside then
    x = 0
  end

  local dist = math.sqrt(x * x + y * y + z * z)
  be:queueJS(string.format("if(window._androidAudio)window._androidAudio.pos(%f,%f,%f,%s,%f);",
    x, y, z, tostring(inside), dist))
end

function M.onUpdate(dt)
  -- 0. Keep JS bridge alive across CEF UI reloads
  jsTimer = jsTimer + dt
  if jsTimer >= 1.5 then
    jsTimer = 0
    ensureJS()
  end

  -- 1. Manage Audio Stream Connection
  if not connected then
    reconnectTimer = reconnectTimer + dt
    if reconnectTimer >= 2.0 then
      reconnectTimer = 0
      connectAudio()
    end
  else
    -- Non-blocking receive: drain all pending chunks
    local chunks = {}
    for _ = 1, 16 do
      local data, err, part = sock:receive(READ_SIZE)
      local got = data or part
      if got and #got > 0 then
        table.insert(chunks, got)
      end
      if err and err ~= "timeout" then
        log('W', 'android_audio', 'Audio socket disconnected: ' .. tostring(err))
        connected = false
        if sock then pcall(function() sock:close() end) end
        sock = nil
        audioProtocolBuffer = ""
        audioRemainder = ""
        audioFormatReady = false
        break
      end
      if not got or #got < READ_SIZE then
        break
      end
    end
    if #chunks > 0 then
      audioProtocolBuffer = audioProtocolBuffer .. table.concat(chunks)
      if #audioProtocolBuffer >= 4 and audioProtocolBuffer:sub(1, 4) == 'BAU1' then
        if #audioProtocolBuffer >= 12 then
          local function u16(pos)
            local a, b = audioProtocolBuffer:byte(pos, pos + 1)
            return a + b * 256
          end
          local function u32(pos)
            local a, b, c, d = audioProtocolBuffer:byte(pos, pos + 3)
            return a + b * 256 + c * 65536 + d * 16777216
          end
          audioSampleRate = u32(5)
          audioChannels = u16(9)
          audioBits = u16(11)
          audioProtocolBuffer = audioProtocolBuffer:sub(13)
          audioFormatReady = audioSampleRate > 0 and audioChannels > 0 and audioBits == 16
        end
      end
      if not audioFormatReady then
        if #audioProtocolBuffer > 64 then
          audioProtocolBuffer = audioProtocolBuffer:sub(-64)
        end
        return
      end
      local raw = audioRemainder .. audioProtocolBuffer
      local bytesPerFrame = math.max(1, audioChannels * audioBits / 8)
      local fullLen = #raw - (#raw % bytesPerFrame)
       if fullLen > 0 then
         local toSend = raw:sub(1, fullLen)
         audioRemainder = raw:sub(fullLen + 1)
        audioProtocolBuffer = ""
        be:queueJS(string.format(
          "if(window._androidAudio)window._androidAudio.push('%s',%d,%d,%d);",
          mime.b64(toSend), audioSampleRate, audioChannels, audioBits))
      else
        audioRemainder = ""
        audioProtocolBuffer = raw
      end
    end
  end

  -- 2. 3D Spatial Audio & Acoustic Muffling tracking
  updatePosition(dt)
end

function M.onUiReady()
  ensureJS()
end

function M.onUiChangedState()
  ensureJS()
end

function M.onScreenTap()
  log('I', 'android_audio', 'Screen trigger tapped: toggling Play/Pause')
  be:queueJS("if(window._androidAudio&&window._androidAudio.resume)window._androidAudio.resume();")
  sendHttpCommand('/media/play_pause')
end

function M.onMediaNext()
  log('I', 'android_audio', 'Media trigger: next track')
  sendHttpCommand('/media/next')
end

function M.onMediaPrev()
  log('I', 'android_audio', 'Media trigger: previous track')
  sendHttpCommand('/media/prev')
end

function M.onExtensionLoaded()
  ensureJS()
  connectAudio()
  log('I', 'android_audio', 'Universal Android 3D Spatial Audio extension ready')
end

function M.onExtensionUnloaded()
  if sock then
    pcall(function() sock:close() end)
    sock = nil
  end
  connected = false
end

return M
