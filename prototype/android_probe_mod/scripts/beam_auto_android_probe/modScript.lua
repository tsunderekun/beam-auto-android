local ok, err = pcall(function()
  log('I', 'beam_auto_android_probe', 'Universal vehicle-relative ImGui Android projection loaded')
  -- extensions.load() deliberately has no success return value in BeamNG;
  -- failures are reported by the extension loader itself.
  extensions.load('beam_auto_android_probe')
  extensions.load('beam_auto_android_audio')
  if setExtensionUnloadMode then
    pcall(setExtensionUnloadMode, 'beam_auto_android_probe', 'manual')
    pcall(setExtensionUnloadMode, 'beam_auto_android_audio', 'manual')
  end
end)

if not ok then
  log('E', 'beam_auto_android_probe', tostring(err))
end
