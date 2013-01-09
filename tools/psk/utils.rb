#
# Common utilities for the psk ruby implementations
#
require 'ostruct'
require 'optparse'
require 'yaml'

HEX=/[0-9A-Fa-f]/

module PMLUtils
  def die(msg)
    $stderr.puts msg
    exit 1
  end
  def warn_once(msg,detail)
    $warn_once ||= {}
    return if $warn_once[msg]
    warn(msg+": "+detail)
    $warn_once[msg]=true
  end
  def warn(msg)
    $stderr.puts "[#{$0}] WARNING #{msg}"
  end
  def get_mbb_label(funcname, blockname)
    ".LBB#{funcname}_#{blockname}"
  end
  def parse_mbb_label(label)
    label =~ /\A\.LBB(\d+)_(\d+)$/
    [$1.to_i, $2.to_i] if $1
  end
end

# class providing convenient additional program information derived from PML
# files
class PML
  attr_reader :data, :functions
  def initialize(io)
    @data = YAML::load(io)
    build_information
  end
  def [](key)
    @data[key]
  end
  def to_s
    @data.to_s
  end
  def bf(name)
    @functions['src'][name]
  end
  def mf(name)
    @functions['dst'][name]
  end
  def build_information
    @functions = { 'src' => {}, 'dst' => {} }
    @dstfunmap = {}
    @data['bitcode-functions'].each { |f| @functions['src'][f['name']] = f }
    @data['machine-functions'].each { |f|
      @functions['dst'][f['name']] = f
      @dstfunmap[f['mapsto']] = f
    }
  end
  def mf_mapping_to(bitcode_name)
    @dstfunmap[bitcode_name]
  end
end

# simple buffer keeping track of the last n values
# last value is accessed with buffer.last, n-th last
# with buffer[-n]
class LastBuffer
  def initialize(k=5,init=[])
    @k, @p = k, 0
    @buf = [nil] * k
    init.each { |v| add(v) }
  end
  def add(v)
    @buf[@p] = v
    @p = (@p+1) % @k
  end
  def last
    self[-1]
  end
  def [](rix)
    raise Exception.new("LastBuffer: index error") if rix >= 0 || rix < -@k
    @buf[(@p+@k+rix)%@k]
  end
end
