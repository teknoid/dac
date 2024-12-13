#-
 - control GP8403 via UDP messages
 - example: for i in 10 20 30 40 50 60 70 80 90 100; do echo p:$i | socat - udp:boiler1:1975; sleep 1; done
 -#

class GP8403_UDP: Driver
  var u

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
      # print(b)

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
        gp8403.set_percent(0, v0)
        # gp8403.set_percent(1, v1)
      end

      if b[0] == 0x76					# 'v'
        gp8403.set_volt(0, v0)
        # gp8403.set_volt(1, v1)
      end

      b = self.u.read()
    end
  end
end

gp8403_udp = GP8403_UDP()
tasmota.add_driver(gp8403_udp)
