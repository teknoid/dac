if tasmota.hostname() == 'boiler1'
  load('GP8403-boiler1.be')
end

if tasmota.hostname() == 'boiler2'
  load('GP8403-boiler2.be')
end

if tasmota.hostname() == 'boiler3'
  load('GP8403-boiler3.be')
end

load('GP8403.be')
load('GP8404-udp.be')