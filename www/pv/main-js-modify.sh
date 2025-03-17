#!/bin/sh

F=pv/main.267cf678607cf5e7.js
cp -v /tmp/main.267cf678607cf5e7.js /server/www/root/pv/pv/

sed -i 's/\/status\/common/\/pv\/status\/common.json/g' "$F"
sed -i 's/\/status\/version/\/pv\/status\/version.json/g' "$F"
sed -i 's/\/status\/network/\/pv\/status\/network.json/g' "$F"
sed -i 's/\/status\/devices/\/pv\/status\/devices.json/g' "$F"
sed -i 's/\/status\/powerflow/\/pv\/status\/powerflow.json/g' "$F"
sed -i 's/\/status\/events/\/pv\/status\/events.json/g' "$F"
sed -i 's/\/status\/activeEvents/\/pv\/status\/activeEvents.json/g' "$F"

sed -i 's/\/config\/wizard/\/pv\/config\/wizard.json/g' "$F"
sed -i 's/\/config\/meter/\/pv\/config\/meter.json/g' "$F"
sed -i 's/\/config\/ohmpilot/\/pv\/config\/ohmpilot.json/g' "$F"
sed -i 's/\/config\/powerunit/\/pv\/config\/powerunit.json/g' "$F"
sed -i 's/\/config\/region-gridcode\/gridcode\/setupinfo/\/pv\/config\/region-gridcode\/gridcode\/setupinfo.json/g' "$F"

sed -i 's/\/components\/cache/\/pv\/components\/cache/g' "$F"
sed -i 's/\/components\/BatteryManagementSystem/\/pv\/components\/BatteryManagementSystem/g' "$F"
sed -i 's/\/components\/HeatPump/\/pv\/components\/HeatPump/g' "$F"
sed -i 's/\/components\/Ohmpilot/\/pv\/components\/Ohmpilot/g' "$F"
sed -i 's/\/components\/PowerMeter/\/pv\/components\/PowerMeter/g' "$F"
