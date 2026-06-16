def command(line)
  case line
  when "help"
    puts "commands: help, echo, version"
  when /^echo (.*)/
    puts $1
  when "version"
    puts RUBY_VERSION
  else
    puts "unknown command: #{line}"
  end
end

puts "Ruby shell loaded from CPIO"
command("help")
command("echo hello from ruby shell")
command("version")

puts "Entering Ruby shell loop"

while true
  puts "ruby> "

  begin
    line = STDIN.gets
  rescue => e
    puts
    puts "stdin unavailable: #{e.class}: #{e.message}"
    line = nil
  end

  if line.nil?
    puts "stdin closed; shell idle"
    while true
    end
  end

  line = line.strip
  next if line.empty?
  break if line == "exit"

  command(line)
end

puts "leaving Ruby shell"
