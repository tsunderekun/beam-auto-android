-- beam_auto_android_probe: RoadWave-style 3D ImGui carrier diagnostic.
-- This is deliberately a diagnostic probe: no phone, ADB or scrcpy.

local M = {}

local socketOk, socket = pcall(require, 'socket')
if not socketOk then socket = rawget(_G, 'socket') end

local devRealW, devRealH = 1080, 2392
local physicalDisplayW, physicalDisplayH = 1080, 2392
local devAspect = devRealW / devRealH
local PHONE_BASE_H = 0.16
local PHONE_WIDTH = PHONE_BASE_H * devAspect
local PHONE_HEIGHT = PHONE_BASE_H
local PHONE_DEPTH = 0.62
local VERTICAL_OFFSET = -0.08
local MAX_CAMERA_DISTANCE = 1.7
local PROBE_MIN_DEPTH, PROBE_MAX_DEPTH = 0.35, 1.2
local PREFS_PATH = 'settings/beam_auto_android_probe.json'
local prefs = { enabled = true, vehicles = {} }
local prefsDirty = false
local cursorVisible, mouseLocked = true, false
local moveMode, dragActive = false, false
local grabU, grabV = 0, 0
local dragStartLat, dragStartUp = 0, 0
local debugVisible = false
local screenVisible = true

local anchor = nil
local anchorVdata = nil
local anchorCameraNode = nil
local previousRef = vec3()
local previousLeft = vec3()
local previousBack = vec3()
local previousValid = false
local renderErrorReported = false
local touchActive = false
local touchU, touchV = 0, 0
local touchStartU, touchStartV = 0, 0
local touchStartTime = 0
local touchState = 'IDLE'
local touchSequence = 0
local TOUCH_QUEUE_CAPACITY = 32
local touchQueue = {}
local touchQueueHead = 1
local touchQueueTail = 1
local touchQueueCount = 0
local touchQueueDropped = 0
local touchEventCount = 0
local touchLastEvent = 'NONE'
local FRAME_CONFIG_PATH = '/temp/beam_auto_android_frame_probe.json'
local frameConfigTimer = 0
local textureRefreshTimer = 0
local framePath = nil
local frameMetadataPath = nil
local screenTex = nil
local frameWidth, frameHeight = 0, 0
local frameLandscape = false
local frameStatus = 'WAITING FOR PRODUCER'
local frameNumber = nil
local deviceMetadataTimer = 0
local lastTouchResetReason = nil
local resetTouch

local function readJsonTable(path, attempts)
  if type(jsonReadFile) ~= 'function' then return nil end
  attempts = attempts or 1
  for _ = 1, attempts do
    local ok, data = pcall(jsonReadFile, path)
    if ok and type(data) == 'table' then return data end
  end
  return nil
end

local function readFrameConfig()
  return readJsonTable(FRAME_CONFIG_PATH, 3)
end

local function clamp(value, lo, hi)
  if type(value) ~= 'number' or value ~= value then return (lo + hi) * 0.5 end
  return math.min(hi, math.max(lo, value))
end

local function applyDeviceInputSize()
  local width, height = physicalDisplayW, physicalDisplayH
  if frameLandscape then width, height = height, width end
  devRealW, devRealH = width, height
  devAspect = devRealW / devRealH
  PHONE_WIDTH = PHONE_BASE_H * devAspect
  PHONE_HEIGHT = PHONE_BASE_H
end

local function loadPrefs()
  local data = readJsonTable(PREFS_PATH, 1)
  if data and type(data.vehicles) == 'table' then prefs = data end
end

local function savePrefs()
  if not prefsDirty or type(jsonWriteFile) ~= 'function' then return end
  prefsDirty = false
  pcall(jsonWriteFile, PREFS_PATH, prefs, true)
end

local function prefsFor(anchorData)
  local key = anchorData.vehKey or 'default'
  local value = prefs.vehicles[key]
  if type(value) ~= 'table' then
    value = { dLat = -0.05, dUp = -0.15, dFwd = 0, scale = 1 }
    prefs.vehicles[key] = value
  end
  value.dLat = tonumber(value.dLat) or 0
  value.dUp = tonumber(value.dUp) or 0
  value.dFwd = tonumber(value.dFwd) or 0
  value.scale = clamp(tonumber(value.scale) or 1, 0.4, 3.5)
  return value
end

local function resetPlacement()
  if anchor and anchor.vehKey then
    prefs.vehicles[anchor.vehKey] = nil
    prefsDirty = true
    savePrefs()
  end
end

local function refreshFrameConfig(im)
  local config = readFrameConfig()
  if not config or config.status ~= 'running' or type(config.vfsFramePath) ~= 'string' then
    if touchActive then resetTouch('producer-disconnected') end
    frameStatus = 'WAITING FOR PRODUCER'
    frameNumber = nil
    framePath = nil
    frameMetadataPath = nil
    screenTex = nil
    frameWidth, frameHeight = 0, 0
    frameLandscape = false
    return
  end

  frameStatus = 'PRODUCER RUNNING'
  local displayWidth = tonumber(config.displayWidth)
  local displayHeight = tonumber(config.displayHeight)
  if displayWidth and displayHeight and displayWidth > 0 and displayHeight > 0 then
    physicalDisplayW, physicalDisplayH = displayWidth, displayHeight
  end
  local configuredFrameNumber = tonumber(config.frameNumber)
  if configuredFrameNumber then frameNumber = configuredFrameNumber end
  frameMetadataPath = type(config.vfsMetadataPath) == 'string' and config.vfsMetadataPath or nil
  if frameMetadataPath then
    local metadata = readJsonTable(frameMetadataPath, 3)
    local metadataFrameNumber = metadata and tonumber(metadata.frameNumber)
    if metadataFrameNumber then frameNumber = metadataFrameNumber end
    local metadataWidth = metadata and tonumber(metadata.width)
    local metadataHeight = metadata and tonumber(metadata.height)
    if metadataWidth and metadataHeight and metadataWidth > 0 and metadataHeight > 0 then
      frameWidth, frameHeight = metadataWidth, metadataHeight
      frameLandscape = metadataWidth > metadataHeight
    end
  end
  applyDeviceInputSize()
  if config.vfsFramePath == framePath and screenTex then return end

  local ok, tex = pcall(im.ImTextureHandler, config.vfsFramePath)
  if ok and tex then
    framePath = config.vfsFramePath
    screenTex = tex
    log('I', 'beamAutoAndroidProbe', 'Synthetic frame texture: ' .. framePath)
  else
    frameStatus = 'TEXTURE HANDLER ERROR'
    log('W', 'beamAutoAndroidProbe', 'Synthetic frame texture unavailable: ' .. tostring(tex))
  end
end

local function basis(refPos, leftPos, backPos)
  local left = leftPos - refPos
  if left:squaredLength() < 1e-8 then return nil end
  left:normalize()

  local up = left:cross(backPos - refPos)
  if up:squaredLength() < 1e-8 then return nil end
  up:normalize()

  return left, up:cross(left), up
end

local function getDriverRefs(vdata)
  return (vdata.cameraRefNodes and vdata.cameraRefNodes.driver)
    or (vdata.refNodes and vdata.refNodes[0])
end

local function buildAnchor(veh, vdata, cameraNode, rhd)
  local refs = getDriverRefs(vdata)
  if not refs then return nil end

  local refRest = veh:getInitialNodePosition(refs.ref)
  local leftRest, backRest = veh:getInitialNodePosition(refs.left), veh:getInitialNodePosition(refs.back)
  local left, back, up = basis(refRest, leftRest, backRest)
  if not left then return nil end

  local eyeRest = veh:getInitialNodePosition(cameraNode)
  local eyeOffset = eyeRest - refRest
  local nodeCount = vdata.nodes and tableSizeC(vdata.nodes) or 0
  local centerLat = 0
  if nodeCount > 0 then
    for i = 0, nodeCount - 1 do
      centerLat = centerLat + (veh:getInitialNodePosition(i) - refRest):dot(left)
    end
    centerLat = centerLat / nodeCount
  end

  return {
    refs = refs,
    eye = {eyeOffset:dot(left), eyeOffset:dot(back), eyeOffset:dot(up)},
    -- Mean lateral node coordinate is the vehicle centreline. Keep the
    -- longitudinal and vertical placement from the driver eye, but remove the
    -- driver's lateral offset so the device sits in the middle of the cabin.
    centerLat = centerLat,
    -- Forward in the vehicle basis is opposite of the cameraRefNodes back axis.
    dir = {0, -1, 0},
    depth = PHONE_DEPTH,
    rhd = rhd and true or false,
    vehKey = veh:getJBeamFilename() .. (rhd and '#rhd' or '#lhd'),
  }
end

local function sendHttpCommand(path)
  if not socket or not socket.tcp then return false end
  local ok, client = pcall(socket.tcp)
  if not ok or not client then return false end
  pcall(client.settimeout, client, 0.08)
  local connected = client:connect('127.0.0.1', 8766)
  if not connected then
    pcall(client.close, client)
    return false
  end
  local request = 'GET ' .. path .. ' HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n'
  local sent = client:send(request)
  pcall(client.receive, client, '*l')
  pcall(client.close, client)
  return sent ~= nil
end

local function androidPoint(u, v)
  return math.floor(clamp(u, 0, 1) * math.max(1, devRealW)),
    math.floor(clamp(v, 0, 1) * math.max(1, devRealH))
end

local function publishGesture()
  local x1, y1 = androidPoint(touchStartU, touchStartV)
  local x2, y2 = androidPoint(touchU, touchV)
  local dx, dy = x2 - x1, y2 - y1
  local distance = math.sqrt(dx * dx + dy * dy)
  local duration = math.floor(clamp((os.clock() - touchStartTime) * 1000, 80, 500))
  if distance < 18 then
    sendHttpCommand(string.format('/tap?x=%d&y=%d', x1, y1))
  else
    sendHttpCommand(string.format('/swipe?x1=%d&y1=%d&x2=%d&y2=%d&duration=%d',
      x1, y1, x2, y2, duration))
  end
end

resetTouch = function(reason)
  -- The legacy backend emits a complete tap/swipe only on release, so there
  -- is no device-side contact to cancel here. Drop the local gesture instead
  -- of inventing a synthetic zero-distance swipe on focus/session loss.
  touchActive = false
  touchState = 'RESET'
  lastTouchResetReason = reason or 'unknown'
end

local function inverseQuad(point, quad)
  local ax = quad[2].x - quad[1].x
  local ay = quad[2].y - quad[1].y
  local bx = quad[3].x - quad[1].x
  local by = quad[3].y - quad[1].y
  local dx = point.x - quad[1].x
  local dy = point.y - quad[1].y
  local det = ax * by - ay * bx
  if math.abs(det) < 1e-6 then return nil end

  return (dx * by - dy * bx) / det,
    (ax * dy - ay * dx) / det
end

local function enqueueTouchEvent(state, u, v)
  local event = {
    state = state,
    u = u or touchU,
    v = v or touchV,
    sequence = touchSequence,
  }
  touchQueue[touchQueueTail] = event
  touchQueueTail = (touchQueueTail % TOUCH_QUEUE_CAPACITY) + 1
  if touchQueueCount == TOUCH_QUEUE_CAPACITY then
    touchQueueHead = (touchQueueHead % TOUCH_QUEUE_CAPACITY) + 1
    touchQueueDropped = touchQueueDropped + 1
  else
    touchQueueCount = touchQueueCount + 1
  end
  touchEventCount = touchEventCount + 1
  touchLastEvent = state
end

local function drainTouchEvent()
  if touchQueueCount == 0 then return nil end
  local event = touchQueue[touchQueueHead]
  touchQueue[touchQueueHead] = nil
  touchQueueHead = (touchQueueHead % TOUCH_QUEUE_CAPACITY) + 1
  touchQueueCount = touchQueueCount - 1
  return event
end

local function updateTouch(im, screenQuad)
  if moveMode then
    if touchActive then resetTouch('placement-mode') end
    return
  end
  local ok, mousePos, clicked, down, released = pcall(function()
    return im.GetMousePos(), im.IsMouseClicked(0), im.IsMouseDown(0),
      im.IsMouseReleased(0)
  end)
  if not ok then
    touchState = 'INPUT API ERROR'
    return
  end

  local u, v = inverseQuad(mousePos, screenQuad)
  local inside = u and v and u >= 0 and u <= 1 and v >= 0 and v <= 1

  if clicked and inside then
    touchActive = true
    touchSequence = touchSequence + 1
    touchU, touchV = u, v
    touchStartU, touchStartV = u, v
    touchStartTime = os.clock()
    touchState = 'DOWN'
    enqueueTouchEvent(touchState, touchU, touchV)
  elseif touchActive and (released or not down) then
    if inside then touchU, touchV = u, v end
    publishGesture()
    touchActive = false
    touchState = 'UP'
    enqueueTouchEvent(touchState, touchU, touchV)
  elseif touchActive and down then
    if inside then touchU, touchV = u, v end
    touchState = inside and 'MOVE' or 'MOVE OUT'
    enqueueTouchEvent(touchState, touchU, touchV)
  elseif inside then
    touchU, touchV = u, v
    touchState = 'HOVER'
  else
    touchState = 'IDLE'
  end
end

local function project(point, camPos, camRight, camForward, camUp, halfTan, aspect)
  local to = point - camPos
  local depth = to:dot(camForward)
  if depth <= 0.05 then return nil end

  local nx = to:dot(camRight) / (depth * halfTan * aspect)
  local ny = to:dot(camUp) / (depth * halfTan)
  return (nx + 1) * 0.5, (1 - ny) * 0.5
end

local function color(im, r, g, b, a)
  return im.GetColorU322(im.ImVec4(r, g, b, a or 1))
end

local function drawLineLoop(im, drawList, points, col, thickness)
  for i = 1, #points do
    im.ImDrawList_AddLine(drawList, points[i], points[(i % #points) + 1], col, thickness)
  end
end

local function drawProjectedQuad(im, drawList, points, fill, border)
  im.ImDrawList_AddQuadFilled(drawList, points[1], points[2], points[4], points[3], fill)
  if border then
    drawLineLoop(im, drawList, {points[1], points[2], points[4], points[3]}, border, 2)
  end
end

local function onPreRenderBody(dt)
  local im = ui_imgui
  if not im or not im.GetBackgroundDrawList1 then return end
  if not core_camera or not core_camera.getPosition then return end

  frameConfigTimer = frameConfigTimer + (dt or 0)
  if frameConfigTimer >= 0.2 then
    frameConfigTimer = 0
    refreshFrameConfig(im)
  end

  local veh = be and be:getPlayerVehicle(0)
  if not veh then
    resetTouch('vehicle-lost')
    return
  end

  local camPos = core_camera.getPosition()
  local cameraNode, rhd = core_camera.getDriverDataById(veh:getID())
  if not cameraNode then
    resetTouch('camera-lost')
    return
  end

  local vehicleData = core_vehicle_manager
    and core_vehicle_manager.getVehicleData(veh:getID())
  local vdata = vehicleData and vehicleData.vdata
  if not vdata then
    resetTouch('vehicle-data-lost')
    return
  end

  if vdata ~= anchorVdata or cameraNode ~= anchorCameraNode then
    anchorVdata = vdata
    anchorCameraNode = cameraNode
    anchor = buildAnchor(veh, vdata, cameraNode, rhd)
    previousValid = false
  end
  if not anchor then
    resetTouch('anchor-lost')
    return
  end

  local currentRef = veh:getNodeAbsPosition(anchor.refs.ref)
  local currentLeft = veh:getNodeAbsPosition(anchor.refs.left)
  local currentBack = veh:getNodeAbsPosition(anchor.refs.back)

  -- BeamNG renders the vehicle mesh from the physics state one frame behind
  -- the state exposed to GE Lua. RoadWave keeps the carrier in that same
  -- timebase: use the previous node sample for this draw, then retain the
  -- fresh sample for the next one. The old 5 m distance guard was wrong: it
  -- froze the carrier during normal driving and then released it in a jump.
  local refPos = previousValid and vec3(previousRef) or currentRef
  local leftPos = previousValid and vec3(previousLeft) or currentLeft
  local backPos = previousValid and vec3(previousBack) or currentBack
  previousRef:set(currentRef)
  previousLeft:set(currentLeft)
  previousBack:set(currentBack)
  previousValid = true

  local left, back, up = basis(refPos, leftPos, backPos)
  if not left then
    resetTouch('basis-lost')
    return
  end

  local cameraNodePos = veh:getNodeAbsPosition(cameraNode)
  local dx = camPos.x - cameraNodePos.x
  local dy = camPos.y - cameraNodePos.y
  local dz = camPos.z - cameraNodePos.z
  if dx * dx + dy * dy + dz * dz > MAX_CAMERA_DISTANCE * MAX_CAMERA_DISTANCE then
    resetTouch('camera-outside')
    return
  end

  local eye = refPos + left * anchor.eye[1] + back * anchor.eye[2] + up * anchor.eye[3]
  local forward = -back
  local right = -left
  local placement = prefsFor(anchor)
  local placementScale = clamp(placement.scale, 0.4, 3.5)
  local placementDepth = clamp(anchor.depth + (tonumber(placement.dFwd) or 0),
    PROBE_MIN_DEPTH, PROBE_MAX_DEPTH)
  local center = refPos + left * anchor.centerLat
    + back * anchor.eye[2] + up * anchor.eye[3]
    + forward * placementDepth + up * VERTICAL_OFFSET
    + right * clamp(tonumber(placement.dLat) or 0, -1.2, 1.2)
    + up * clamp(tonumber(placement.dUp) or 0, -1.0, 1.0)

  local camForward = core_camera.getForward()
  local camUp = core_camera.getUp()
  local camRight = core_camera.getRight and core_camera.getRight() or camForward:cross(camUp)
  local viewport = im.GetMainViewport()
  local sw, sh = viewport.Size.x, viewport.Size.y
  if sw <= 0 or sh <= 0 then return end

  local fov = core_camera.getFovRad and core_camera.getFovRad()
    or math.rad(core_camera.getFovDeg())
  local halfTan = math.tan(fov * 0.5)
  local aspect = sw / sh

  local halfWidth = right * (PHONE_WIDTH * placementScale * 0.5)
  local halfHeight = up * (PHONE_HEIGHT * placementScale * 0.5)
  local worldPoints = {
    center - halfWidth + halfHeight,
    center + halfWidth + halfHeight,
    center - halfWidth - halfHeight,
    center + halfWidth - halfHeight,
  }

  local screenPoints = {}
  for i = 1, 4 do
    local x, y = project(worldPoints[i], camPos, camRight, camForward, camUp, halfTan, aspect)
    if not x then return end
    screenPoints[i] = im.ImVec2(viewport.Pos.x + x * sw, viewport.Pos.y + y * sh)
  end

  local dl = im.GetBackgroundDrawList1()
  local shadow = color(im, 0, 0, 0, 0.42)
  local frame = color(im, 0.10, 0.11, 0.13, 0.98)
  local frameBorder = color(im, 0.30, 0.33, 0.38, 0.90)
  local screen = color(im, 0.01, 0.01, 0.02, 1.0)
  local screenBorder = color(im, 0.20, 0.22, 0.28, 0.70)
  local white = color(im, 1, 1, 1, 1)
  local dark = color(im, 0.02, 0.02, 0.02, 0.95)

  local function outerPoint(u, v)
    local topX = screenPoints[1].x + (screenPoints[2].x - screenPoints[1].x) * u
    local topY = screenPoints[1].y + (screenPoints[2].y - screenPoints[1].y) * u
    local bottomX = screenPoints[3].x + (screenPoints[4].x - screenPoints[3].x) * u
    local bottomY = screenPoints[3].y + (screenPoints[4].y - screenPoints[3].y) * u
    return im.ImVec2(topX + (bottomX - topX) * v,
      topY + (bottomY - topY) * v)
  end

  local function roundedPoints(u0r, v0r, u1r, v1r, radius, segments)
    local ru = math.min(radius, (u1r - u0r) * 0.45)
    local rv = math.min(radius, (v1r - v0r) * 0.45)
    local points = {}
    local function addArc(cx, cy, a0, a1)
      for i = 0, segments do
        local angle = a0 + (a1 - a0) * i / segments
        points[#points + 1] = outerPoint(cx + ru * math.cos(angle),
          cy + rv * math.sin(angle))
      end
    end
    addArc(u1r - ru, v0r + rv, -math.pi * 0.5, 0)
    addArc(u1r - ru, v1r - rv, 0, math.pi * 0.5)
    addArc(u0r + ru, v1r - rv, math.pi * 0.5, math.pi)
    addArc(u0r + ru, v0r + rv, math.pi, math.pi * 1.5)
    return points
  end

  local function fillRounded(points, fill)
    local center = outerPoint(0.5, 0.5)
    for i = 1, #points do
      im.ImDrawList_AddTriangleFilled(dl, center, points[i],
        points[(i % #points) + 1], fill)
    end
  end

  local function borderRounded(points, border, thickness)
    for i = 1, #points do
      im.ImDrawList_AddLine(dl, points[i], points[(i % #points) + 1], border, thickness)
    end
  end

  if screenVisible then
    local outerRounded = roundedPoints(-0.015, -0.010, 1.015, 1.020, 0.045, 7)
    fillRounded(outerRounded, shadow)
    local frameRounded = roundedPoints(0, 0, 1, 1, 0.038, 7)
    fillRounded(frameRounded, frame)
    borderRounded(frameRounded, frameBorder, 1)
  end

  -- Keep the bezel narrow; the old 8% inset made the phone look like a
  -- thick plastic frame rather than a modern display.
  local u0, u1, v0, v1 = 0.035, 0.965, 0.03, 0.97
  local function bilinear(u, v)
    local topX = screenPoints[1].x + (screenPoints[2].x - screenPoints[1].x) * u
    local topY = screenPoints[1].y + (screenPoints[2].y - screenPoints[1].y) * u
    local bottomX = screenPoints[3].x + (screenPoints[4].x - screenPoints[3].x) * u
    local bottomY = screenPoints[3].y + (screenPoints[4].y - screenPoints[3].y) * u
    return im.ImVec2(topX + (bottomX - topX) * v, topY + (bottomY - topY) * v)
  end

  local screenQuad = {bilinear(u0, v0), bilinear(u1, v0), bilinear(u0, v1), bilinear(u1, v1)}

  -- Placement controls deliberately live outside the screen quad, so they
  -- cannot become Android touches. The phone remains vehicle-relative while
  -- its local offset, depth and size are edited and persisted per vehicle.
  local mousePos = im.GetMousePos()
  local placementU, placementV = inverseQuad(mousePos, screenPoints)
  local placementInside = placementU and placementV and placementU >= -0.25
    and placementU <= 1.25 and placementV >= -0.24 and placementV <= 1.15
  local clicked = im.IsMouseClicked(0)
  local down = im.IsMouseDown(0)
  local doubleClicked = im.IsMouseDoubleClicked and im.IsMouseDoubleClicked(0)
  local io = im.GetIO and im.GetIO()
  local wheel = io and tonumber(io.MouseWheel) or 0
  local shift = io and io.KeyShift or false
  if clicked and placementInside and placementV >= -0.20 and placementV <= -0.04 then
    if placementU >= 0.02 and placementU <= 0.31 then
      debugVisible = not debugVisible
    elseif placementU >= 0.35 and placementU <= 0.64 then
      if doubleClicked then resetPlacement() end
      moveMode = not moveMode
      dragActive = false
      if not moveMode then savePrefs() end
    elseif placementU >= 0.68 and placementU <= 0.98 then
      screenVisible = not screenVisible
      if not screenVisible then resetTouch('screen-hidden') end
    end
  elseif moveMode and clicked and placementInside and placementV >= 0 and placementV <= 1 then
    dragActive = true
    grabU, grabV = placementU, placementV
    dragStartLat, dragStartUp = placement.dLat, placement.dUp
  elseif moveMode and dragActive and not down then
    dragActive = false
    savePrefs()
  end
  if moveMode and dragActive and down and placementU and placementV then
    placement.dLat = clamp(dragStartLat + (placementU - grabU) * PHONE_WIDTH * placementScale,
      -1.2, 1.2)
    placement.dUp = clamp(dragStartUp - (placementV - grabV) * PHONE_HEIGHT * placementScale,
      -1.0, 1.0)
    prefsDirty = true
  end
  if moveMode and wheel ~= 0 and placementInside then
    if shift then
      placement.dFwd = clamp((tonumber(placement.dFwd) or 0) - wheel * 0.03,
        PROBE_MIN_DEPTH - anchor.depth, PROBE_MAX_DEPTH - anchor.depth)
    else
      placement.scale = clamp(placementScale * (1 + wheel * 0.08), 0.4, 3.5)
    end
    prefsDirty = true
  end
  if screenVisible then
    updateTouch(im, screenQuad)
    drainTouchEvent()
    textureRefreshTimer = textureRefreshTimer + (dt or 0)
    local scrTexId = nil
    if screenTex and framePath and textureRefreshTimer >= 0.033 then
      textureRefreshTimer = 0
      pcall(screenTex.setID, screenTex, framePath)
      if FS and FS.triggerFilesChanged then
        pcall(FS.triggerFilesChanged, FS, {framePath})
      end
    end
    if screenTex then
      local okId, id = pcall(screenTex.getID, screenTex)
      if okId and id then scrTexId = id end
    end
    if scrTexId then
      im.ImDrawList_AddImageQuad(dl, scrTexId,
        screenQuad[1], screenQuad[2], screenQuad[4], screenQuad[3],
        im.ImVec2(0, 0), im.ImVec2(1, 0), im.ImVec2(1, 1), im.ImVec2(0, 1),
        color(im, 1, 1, 1, 1))
    else
      im.ImDrawList_AddQuadFilled(dl, screenQuad[1], screenQuad[2], screenQuad[4], screenQuad[3], screen)
    end
    -- ImGui has no texture clipping path for this projected quad. Mask the
    -- four corner wedges after drawing the texture, leaving a real rounded
    -- screen opening instead of only a rounded outline.
    local screenRounded = roundedPoints(u0, v0, u1, v1, 0.035, 7)
    local function maskCorner(squareCorner, first, last)
      for i = first, last - 1 do
        im.ImDrawList_AddTriangleFilled(dl, squareCorner, screenRounded[i],
          screenRounded[i + 1], frame)
      end
    end
    -- screenRounded order: top-right, bottom-right, bottom-left, top-left.
    maskCorner(screenQuad[2], 1, 8)
    maskCorner(screenQuad[4], 9, 16)
    maskCorner(screenQuad[3], 17, 24)
    maskCorner(screenQuad[1], 25, 32)
    drawLineLoop(im, dl, {screenQuad[1], screenQuad[2], screenQuad[4], screenQuad[3]}, screenBorder, 1)
    im.ImDrawList_AddCircleFilled(dl, bilinear(0.50, 0.026), 4, color(im, 0.01, 0.01, 0.01, 1))
    if touchActive and down then
      im.ImDrawList_AddCircleFilled(dl, bilinear(touchU, touchV), 8, color(im, 1, 1, 1, 0.30))
      im.ImDrawList_AddCircle(dl, bilinear(touchU, touchV), 15, color(im, 1, 1, 1, 0.18), 16, 1.5)
    end
    if not scrTexId then
      im.ImDrawList_AddText1(dl, bilinear(0.18, 0.46), dark, 'ANDROID 3D PROBE', nil)
      im.ImDrawList_AddText1(dl, bilinear(0.19, 0.54), dark, 'IMGUI VEHICLE ANCHOR', nil)
    end
  else
    if touchActive then resetTouch('screen-hidden') end
  end

  local function outerPoint(u, v)
    local topX = screenPoints[1].x + (screenPoints[2].x - screenPoints[1].x) * u
    local topY = screenPoints[1].y + (screenPoints[2].y - screenPoints[1].y) * u
    local bottomX = screenPoints[3].x + (screenPoints[4].x - screenPoints[3].x) * u
    local bottomY = screenPoints[3].y + (screenPoints[4].y - screenPoints[3].y) * u
    return im.ImVec2(topX + (bottomX - topX) * v,
      topY + (bottomY - topY) * v)
  end
  local controlFill = color(im, 0.05, 0.06, 0.08, 0.94)
  local controlBorder = color(im, 0.55, 0.60, 0.70, 0.95)
  local controlText = color(im, 0.92, 0.94, 0.98, 1)
  -- Keep the controls out of the touch surface. They are only painted while
  -- the pointer is in a small activation band above the display, so a normal
  -- tap near the top edge cannot land on a permanently visible control.
  local controlsHover = placementU and placementV
    and placementU >= -0.02 and placementU <= 1.02
    and placementV >= -0.24 and placementV <= 0.06
  if controlsHover then
    local debugHover = placementU >= 0.02 and placementU <= 0.31
      and placementV >= -0.20 and placementV <= -0.04
    local moveHover = placementU >= 0.35 and placementU <= 0.64
      and placementV >= -0.20 and placementV <= -0.04
    local visibilityHover = placementU >= 0.68 and placementU <= 0.98
      and placementV >= -0.20 and placementV <= -0.04
    local debugButtonColor = debugHover and color(im, 0.16, 0.20, 0.28, 0.98) or controlFill
    local moveButtonColor = moveHover and color(im, 0.16, 0.20, 0.28, 0.98) or controlFill
    local visibilityButtonColor = visibilityHover and color(im, 0.16, 0.20, 0.28, 0.98) or controlFill
    im.ImDrawList_AddQuadFilled(dl, outerPoint(0.02, -0.20), outerPoint(0.31, -0.20),
      outerPoint(0.31, -0.04), outerPoint(0.02, -0.04), debugButtonColor)
    drawLineLoop(im, dl, {outerPoint(0.02, -0.20), outerPoint(0.31, -0.20),
      outerPoint(0.31, -0.04), outerPoint(0.02, -0.04)}, controlBorder, 1)
    im.ImDrawList_AddText1(dl, outerPoint(0.06, -0.155), controlText,
      debugVisible and '[D] Hide debug' or '[D] Debug', nil)
    im.ImDrawList_AddQuadFilled(dl, outerPoint(0.35, -0.20), outerPoint(0.64, -0.20),
      outerPoint(0.64, -0.04), outerPoint(0.35, -0.04), moveButtonColor)
    drawLineLoop(im, dl, {outerPoint(0.35, -0.20), outerPoint(0.64, -0.20),
      outerPoint(0.64, -0.04), outerPoint(0.35, -0.04)}, controlBorder, 1)
    im.ImDrawList_AddText1(dl, outerPoint(0.39, -0.155), controlText,
      moveMode and '[#] Done' or '[#] Move', nil)
    im.ImDrawList_AddQuadFilled(dl, outerPoint(0.68, -0.20), outerPoint(0.98, -0.20),
      outerPoint(0.98, -0.04), outerPoint(0.68, -0.04), visibilityButtonColor)
    drawLineLoop(im, dl, {outerPoint(0.68, -0.20), outerPoint(0.98, -0.20),
      outerPoint(0.98, -0.04), outerPoint(0.68, -0.04)}, controlBorder, 1)
    im.ImDrawList_AddText1(dl, outerPoint(0.72, -0.155), controlText,
      screenVisible and '[S] Hide' or '[S] Show', nil)
  end
  if moveMode then
    im.ImDrawList_AddText1(dl, outerPoint(0.05, 1.03), controlText,
      'Drag: move   Wheel: size   Shift+Wheel: depth   Double-click: reset', nil)
  end
  if screenVisible and debugVisible then
    local debugPanel = color(im, 0.02, 0.025, 0.035, 0.88)
    local debugText = color(im, 0.82, 0.86, 0.92, 1)
    im.ImDrawList_AddQuadFilled(dl, outerPoint(0.06, 0.10), outerPoint(0.94, 0.10),
      outerPoint(0.94, 0.30), outerPoint(0.06, 0.30), debugPanel)
    im.ImDrawList_AddText1(dl, outerPoint(0.09, 0.13), debugText,
      string.format('TOUCH %s  #%d  UV %.3f %.3f', touchState, touchSequence,
        touchU, touchV), nil)
    im.ImDrawList_AddText1(dl, outerPoint(0.09, 0.20), debugText,
      string.format('FRAME %s  %s  %s', frameNumber and tostring(frameNumber) or '--',
        frameStatus, rhd and 'RHD' or 'LHD'), nil)
    im.ImDrawList_AddText1(dl, outerPoint(0.09, 0.27), debugText,
      string.format('QUEUE %d/%d  LAST %s  DROP %d', touchQueueCount,
        touchEventCount, touchLastEvent, touchQueueDropped), nil)
  end
end

function M.onPreRender(...)
  local ok, err = pcall(onPreRenderBody, ...)
  if not ok and not renderErrorReported then
    renderErrorReported = true
    log('E', 'beamAutoAndroidProbe', 'onPreRender failed: ' .. tostring(err))
  end
end

function M.onExtensionLoaded()
  loadPrefs()
  log('I', 'beamAutoAndroidProbe', 'RoadWave-style ImGui carrier probe loaded')
end

function M.onExtensionUnloaded()
  resetTouch('extension-unloaded')
  savePrefs()
end

function M.onUiChangedState(toState)
  if toState and toState ~= 'play' and toState ~= 'pause.photomode' then
    resetTouch('ui-state')
  end
end

function M.onCursorVisibilityChanged(visible)
  cursorVisible = visible and true or false
  if not cursorVisible then resetTouch('cursor-hidden') end
end

function M.onMouseLocked(locked)
  mouseLocked = locked and true or false
  if mouseLocked then resetTouch('mouse-locked') end
end

return M
