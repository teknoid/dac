#-
 - Berry I2C Driver for Gravity I2C DAC Module (0-10V) GP8403
 - this module is used to control an A-SENCO SCR-802 SSR Power Regulator
 -#

class GP8403: Driver
  static NAME = "GP8403"	# module name
  static ADDR = 0x5f		# I2C address of module

  static LED = 2			# LED
  static LED_FLASH = [ 30, 30, 30, 30, 30, 27, 22, 17, 13, 10, 8, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0 ]

  var wire
  var vc0					# voltage channel 0
  var vc1					# voltage channel 1
  var flash

  def init()
    self.vc0 = -1
    self.vc1 = -1
    self.flash = 0

    gpio.digital_write(self.LED, 0)
    self.wire = tasmota.wire_scan(self.ADDR)

    if self.wire
      # print("I2C: "..self.NAME.." detected on bus "+str(self.wire.bus))
      self.wire.write(self.ADDR, 0x01, 0x11, 1)		#- initialize GP8403 with 0..10V -#
    else
      return
    end

    self.set_volt(0, 0)
    self.set_volt(1, 0)
  end
  
  def set_volt(channel, volt)
    if !self.wire return end

    if volt < 0 volt = 0 end
    if volt > 10000 volt = 10000 end
    # print("channel "+str(channel).." volt "+str(volt))

    var value = volt * 4095 / 10000
    # print("volt "+str(volt).." value "+str(value))
    var vlo = value << 4 & 0xf0
    var vhi = value >> 4 & 0xff
    # print("vlo "+str(vlo).." vhi "+str(vhi))

    if channel == 0 && volt != self.vc0
      self.wire._begin_transmission(self.ADDR)
      self.wire._write(0x02)
      self.wire._write(vlo)
      self.wire._write(vhi)
      self.wire._end_transmission(true)
      self.vc0 = volt
	end	
    if channel == 1 && volt != self.vc1
      self.wire._begin_transmission(self.ADDR)
      self.wire._write(0x04)
      self.wire._write(vlo)
      self.wire._write(vhi)
      self.wire._end_transmission(true)
      self.vc1 = volt
	end	
  end

  def set_percent(channel, percent)
    if !self.wire return end

    if percent < 0 percent = 0 end
    if percent > 100 percent = 100 end
    # print("channel "+str(channel).." percent "+str(percent))

    var volt = gp8403_phase_angles.SSR_PHASE_ANGLE[percent]
    self.set_volt(channel, volt)
  end

  #- display channel0/channel1 value in the web UI -#
  def web_sensor()
    if !self.wire return end

    import string
    var msg = string.format(
             "{s}GP8403 channel0 %i mV {s}GP8403 channel1 %i mV",
              self.vc0, self.vc1)
    tasmota.web_send_decimal(msg)
  end

  #- add channel0/channel1 value to teleperiod -#
  def json_append()
    if !self.wire return end 

    import string
    var msg = string.format(",\"GP8403\":{\"channel0\":%i,\"channel1\":%i}",
              self.vc0, self.vc1)
    tasmota.response_append(msg)
  end

  #- visualize channel0 by flashing LED at GPIO2 -#
  def every_50ms()
    if gpio.digital_read(self.LED) == 0
      if self.vc0 > 0
        if self.flash > 0
          self.flash = self.flash - 1
          return
        end
      	gpio.digital_write(self.LED, 1)			# switch LED on when timer is expired and voltage > 0 
      end
    else
      if self.vc0 < 10000
        gpio.digital_write(self.LED, 0)			# switch LED off when not at full voltage
        var index = self.vc0 / 500				# 0->0 10000->20
        self.flash = self.LED_FLASH[index]		# start timer for next switch on
        # print(self.flash)
      end
    end
  end
end

gp8403 = GP8403()
tasmota.add_driver(gp8403)
