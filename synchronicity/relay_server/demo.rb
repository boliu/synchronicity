require 'net/http'
require 'optparse'
require 'socket'

Signal.trap("INT") do
  puts ""
  exit
end

def debug(*msg)
  puts *msg.join(" ")
end

$options = {
  :vlcport => 8080,
  :server => 'linux002.student.cs.uwaterloo.ca',
  :serverport => 6789,
  :code => 'N234567890123456'
}

OptionParser.new do |opts|
  opts.banner = "Usage: ruby demo.rb [options]"

  opts.on("-p", "--vlcport port", Integer, "port for VLC [default: %d]" % $options[:vlcport]) do |vlcport|
    $options[:vlcport] = vlcport
  end

  opts.on("-s", "--server server", String, "routing server [default: %s]" % $options[:server]) do |server|
    $options[:server] = server
  end

  opts.on("-c", "--code code", String, "connection code [default: %s]" % $options[:code]) do |code|
    $options[:code] = code
  end

  opts.on("-h", "--help", "Show this message") do
    puts opts
    exit
  end
end.parse!

debug($options.inspect)

#uri = URI.parse($options[:room_url])
partner = TCPSocket.open($options[:server], $options[:serverport])

class VlcSocketControl
  STOPPED = 'stop'
  PLAYING = 'playing'
  PAUSED = 'paused'
  
  def play
    pl_pause unless get_status == PLAYING
  end

  def pause
    pl_pause if get_status == PLAYING
  end

  def get_status
    status_xml = Net::HTTP.get('localhost', '/requests/status.xml', $options[:vlcport])
    md = status_xml.match(/<state>(\w+)<\/state>/)
    return md[1];
  end

  def pl_pause
    Net::HTTP.get('localhost', '/requests/status.xml?command=pl_pause', $options[:vlcport])
  end
end

$vsc = VlcSocketControl.new
partner << $options[:code]
while(true)
  read,write,e = select([partner, STDIN])
  test = read[0].gets

  case read[0]
  when partner then puts test
  when STDIN   then partner.puts test
  end


  case test.strip
  when 'paused' then $vsc.pause
  when 'playing' then $vsc.play
  end
end
