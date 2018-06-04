#Last modified : 20130503 13:25

$input_name = ARGV[0]
$pids = []
$unfinished=[]
$fd_ignore=[]
$fd_open=[]
$new_id = 0
$is_first_time=0
$first_time=0

#methods

def insert_pid(pid)
	$pids.each{ |id|
		if(id == pid)
			return;
		end
	}
	$pids[$pids.size] = pid
end

def insert_fd(new_fd)
  $fd_ignore.each{ |fd|
    if(fd == new_fd)
      return
    end
    }
  $fd_ignore.push(new_fd)
  #print "insert fd ", new_fd, "\n"
end

def remove_fd(new_fd)
  return $fd_ignore.delete(new_fd)
  #print "remove fd ", new_fd, "\n"
end

def check_fd(new_fd)
  $fd_ignore.each{ |fd|
    if(fd == new_fd)
      #puts("match")
      return 1
    end
    }
    return 0
end

def insert_open_fd(new_fd)
  $fd_open.each{ |fd|
    if(fd == new_fd)
      return
    end
    }
  $fd_open.push(new_fd)
  #print "insert fd ", new_fd, "\n"
end

def remove_open_fd(new_fd)
  return $fd_open.delete(new_fd)
  #print "remove fd ", new_fd, "\n"
end

def check_open_fd(new_fd)
  $fd_open.each{ |fd|
    if(fd == new_fd)
      #puts("match")
      return 1
    end
    }
    return 0
end

def parse_pid(line)
	str=line.split(" ")
	pid = str[0].to_i
	insert_pid(pid)
	#if( line.include? "open" )
		#if not( (line.include? "ashmem") || (line.include? "dev") )
		
		#end
	#end
end

def add_unfinished(line)
	tmp_line = line
	tmp_line=tmp_line[0..(tmp_line.index("<")-2)]
	$unfinished.push(tmp_line);
	#puts($unfinished[$unfinished.size-1])
end

def get_unfinished(line)
	if($unfinished.size == 0)
		return nil
	end
	if($unfinished.size > 1)
    $unfinished.delete_at(0)
  end
	#puts($unfinished.size)
	str=$unfinished[0].split
	cmd1 = str[2][0..(str[2].index("(")-1)]
	cmd2 = line[(line.index("<")+5)..(line.index(">")-9)]
	#puts(cmd1)
	#puts(cmd2)
	if(cmd1==cmd2)
		ret_str=$unfinished[0].concat(line[(line.index(">")+2)..(line.size-1)])
		#puts(ret_str)
	end
	$unfinished.delete_at(0)
	return ret_str
end

def parse_line(line, pid, new_id)

	parsed_line=nil;
	str=line.split(" ")
	if(str[0].to_i == 0)
		return
	end
	if(str[0].to_i != pid)
		return
	end
  #puts(line)
  
  #########################################
  # skip useless line
  #########################################
  if(line == nil) 
   return
  elsif(str[2] == "---")
    return
  elsif(line.include?("interrupted"))
   return
  elsif(line.include?("detached"))
   return
  elsif(line.include?("+++"))
    return
  end
  
  
  if($is_first_time==0)
     $is_first_time=1
     $first_time=str[1].to_f
  end 

  #########################################
  # combine "unfinished" + "resumed" line
  #########################################
	if (line.include? "unfinished")
		#puts(line)
		add_unfinished(line)
	elsif(line.include? "resumed")
		#puts(get_unfinished(line))
		parsed_line=get_unfinished(line)
	else
		#puts(line)
		parsed_line=line
	end
	
	#########################################
	# skip useless line
	#########################################
	if(parsed_line == nil) 
	 return
	end

	#puts(parsed_line)
    
  #########################################
  # split parsed_line and find return_value
  #########################################    
		str=parsed_line.split
		return_value=nil
		index = 0
		fd =nil
		err_str=nil
		str.each do |item|
			#puts(item)
			if(item == "=")
				return_value=str[index+1].to_i
				err_str = str[index+2]
			end
			index+=1
		end
		
	#########################################
  # remove error string, "...", {}
  #########################################
		if(err_str != nil)
		  parsed_line=parsed_line[0..(parsed_line.index(err_str)-1)]
		end
		
		if(parsed_line.include?("..."))
		 parsed_line =parsed_line[0..(parsed_line.index("...")-1)].concat(parsed_line[parsed_line.index("...")+3..parsed_line.size])
		end
		
		if(parsed_line.include?("[") && parsed_line.include?("]"))
      parsed_line =parsed_line[0..(parsed_line.index("[")-1)].concat(parsed_line[parsed_line.index("]")+1..parsed_line.size])
    end
		
		if(parsed_line.include?("{") && parsed_line.include?("}"))
		  parsed_line =parsed_line[0..(parsed_line.index("{")-1)].concat(parsed_line[parsed_line.index("}")+1..parsed_line.size])
		end
		
	#########################################
  # get items from splitted str
  #########################################
   
  	#puts(parsed_line)
	if(str[0] == nil) 
	     return
	end 
		time = str[1]
		cmd = str[2][0..(str[2].index("(")-1)]
		args = parsed_line[(parsed_line.index("(")+1)..(parsed_line.index(")")-1)]
    args_str = args.split(", ")
    #puts(args)
    if(args_str[0] != nil)
      partition = args_str[0].split("/")
    end
  
		if(cmd.include?("open") || cmd.include?("access") || cmd.include?("stat") || cmd.include?("unlink") || cmd.include?("close")) || cmd.include?("sync")
			#puts("nofd")
			if not(cmd.include?("close"))
			  str2=str[2].split('"')
			  target = str2[1]
			   #target = str[2][(str[2].index("(")+2)..(str[2].index(",")-2)]
			   if(target != nil && target.include?("ashmem"))
			     insert_fd(return_value)
			     return
			   end
			else
			  close_fd = str[2][(str[2].index("(")+1)..(str[2].index(")")-1)]
			  if(remove_fd(close_fd)!=nil)
			    return
			  end
			end
		else
		  if(str[2].include?(","))
  			fd = str[2][(str[2].index("(")+1)..(str[2].index(",")-1)]
  			if(check_fd(fd) == 1)
  			  #puts(parsed_line)
  			  return
  			end
  		end
		end
			
	#########################################
  # get relative time : usecs
  #########################################
  #  time = str[1]
	#	if($is_first_time==0)
	#	  $is_first_time=1
	#	  $first_time=time.to_f
	#	end	
			time=(((time.to_f-$first_time)*1000000).round).to_s

  #########################################
  # Find IO without opened fd, and Fix it
  #########################################
    if(cmd.include?("open"))
      insert_open_fd(fd)
    elsif(cmd.include?("write") || cmd.include?("pwrite") || cmd.include?("read") || cmd.include?("pread") || cmd.include?("sync") || cmd.include?("fstat"))
      if(check_open_fd(fd) == 0)
	  	if(fd != nil)
        	insert_open_fd(fd)
        	$fo.print new_id," ", time," open /data/data O_WRONLY|O_CREAT|O_TRUNC|O_LARGEFILE ",fd,"\n"
		end
      end
    end

  #########################################
  # Print out command lines
  #########################################
  
		#puts(cmd)
		#puts(parsed_line)
	
		if(cmd.include?("access"))
		  $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",return_value,"\n"
		elsif(cmd.include?("open"))
		  if(args_str[1].include?("O_RDONLY"))  # read only
		    $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",args_str[1]," ",return_value,"\n"
		  else
		    $fo.print new_id," ", time," ",cmd," ", partition[1],"/",partition[2]," ",args_str[1]," ",return_value,"\n"
		  end
		elsif((cmd.include?("stat")) && (not cmd.include?("fstat")))
		  $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",return_value,"\n"
		elsif(cmd.include?("fstat"))
      $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",return_value,"\n"
    elsif((cmd.include?("write")) && (not cmd.include?("pwrite")))
      $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",return_value,"\n"
    elsif(cmd.include?("pwrite"))
      $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",args_str[3].to_i/(2**32)," ",return_value,"\n"
    elsif((cmd.include?("read")) && (not cmd.include?("pread")))
      $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",return_value,"\n"
    elsif(cmd.include?("pread"))
      $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",args_str[3].to_i/(2**32)," ",return_value,"\n"
    elsif(cmd.include?("close"))
      $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",return_value,"\n"
    elsif(cmd.include?("unlink"))
      $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",return_value,"\n"
    elsif(cmd.include?("sync"))
      $fo.print new_id," ", time," ",cmd," ", args_str[0]," ",return_value,"\n"
    end
	
end #end of method "parse_line"


#main code
$fo=File.open($input_name+"_mg.out", "w+")

#for i in $input_name
  
	f=File.open($input_name, "r+")
	# parse pid from each line
		f.each_line do |line|
		parse_pid(line.unpack("C*").pack("U*"))
		end
	
	# parse command from each pid
	$pids.each do |id|
		f.seek(0, IO::SEEK_SET)
		f.each_line do |line|
			parse_line(line.unpack("C*").pack("U*"), id, $new_id)
		end
		$new_id += 1
	end

	f.close
	
#end #end of for loop

$fo.close
