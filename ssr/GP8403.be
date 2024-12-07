#-
 - Berry I2C Driver for Gravity I2C DAC Module (0-10V) GP8403
 - this module is used to control an A-SENCO SCR-802 SSR Power Regulator
 -#

class GP8403: Driver
  static NAME = "GP8403"	# module name
  static ADDR = 0x5f		# I2C address of module
 
  # convert 0..100% output power into 0..10V input control voltage
  static SSR_PHASE_ANGLE = [ 
    0, 2760, 2900, 3010, 3080, 3140, 3200, 3250, 3300, 3350, 3400, 
       3450, 3500, 3550, 3580, 3620, 3660, 3700, 3750, 3780, 3820, 
       3870, 3890, 3940, 3980, 4010, 4050, 4080, 4110, 4150, 4190, 
       4220, 4260, 4300, 4330, 4380, 4420, 4460, 4490, 4520, 4570, 
       4610, 4640, 4680, 4730, 4780, 4810, 4860, 4900, 4930, 4980, 
       5040, 5080, 5120, 5180, 5220, 5280, 5320, 5370, 5400, 5450, 
       5530, 5590, 5630, 5670, 5740, 5770, 5850, 5910, 5970, 6030, 
       6090, 6140, 6220, 6290, 6370, 6420, 6500, 6560, 6630, 6710, 
       6780, 6900, 6980, 7090, 7200, 7310, 7440, 7600, 7780, 7900, 
       8080, 8240, 8410, 8960, 9390, 9900, 9960, 9999, 9999, 10000
  ]
   
  var u
  var wire
  var vc0					# voltage channel 0
  var vc1					# voltage channel 1

  def init()
    self.wire = tasmota.wire_scan(self.ADDR)
    
    if self.wire
      print("I2C: "..self.NAME.." detected on bus "+str(self.wire.bus))
      self.wire.write(self.ADDR, 0x01, 0x11, 1)		#- initialize GP8403 with 0..10V -#
    else
      return
    end

    self.vc0 = -1
    self.vc1 = -1
    
    self.set_volt(0, 0)
    self.set_volt(1, 0)
  end

  def set_volt(channel, volt)
    if !self.wire return end
    
    if volt < 0 volt = 0 end
    if volt > 10000 volt = 10000 end
    print("channel "+str(channel).." volt "+str(volt))
    
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
    print("channel "+str(channel).." percent "+str(percent))
  
    var volt = self.SSR_PHASE_ANGLE[percent]
    self.set_volt(channel, volt)
  end
  
  #- setup UDP packet handler to accept messages like "v:12345:56789" or "p:50:100" -#
  def every_50ms()
    # wait for wifi
    if !tasmota.wifi return end
    if !tasmota.wifi('up') return end
    if !self.u
      self.u = udp()
      self.u.begin("", 1975)
    end
    
    # import string
    var b = self.u.read()
    while b != nil
      # tasmota.log(string.format(">>> Received packet ([%s]:%i): %s", self.u.remote_ip, self.u.remote_port, b), 2)
      print(b)

      var x = 0
      var y = 0
      var z = size(b)-1
 
      for i:0..z
        if b[i] == 0x3a 				# ':'
          if x == 0
            x = i 						# first :
          else
            y = i 						# second :
          end
        end
      end
      
      var v0 = 0
      if x
        var b0 = b[x+1..y-1]			# first number
        v0 = number(b0.asstring())
        # print(v0)
      end
      
      var v1 = 0
      if y
        var b1 = b[y+1..z-1]			# second number
        v1 = number(b1.asstring())
        # print(v1)
      end

      if b[0] == 0x70					# 'p'
        self.set_percent(0, v0)
        self.set_percent(1, v1)
      end
      
      if b[0] == 0x76					# 'v'
        self.set_volt(0, v0)
        self.set_volt(1, v1)
      end
      
      b = self.u.read()
    end
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
end

gp8403 = GP8403()
tasmota.add_driver(gp8403)
