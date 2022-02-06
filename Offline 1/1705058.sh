#!/bin/bash

#A Function for getting Input File Name and Working Directory From User
GetInput(){
	OutputDirectory="output_dir"
	WorkingDirectory=""
	InputFile=""

	if (($#==0)); 
	then
		echo 'Use the script like this: ./1705058.sh Working_Directory(Optional) Input_File_Name'
		exit -1
	elif (($#==1)); 
	then
		InputFile=$1
		WorkingDirectory=$(pwd)
		if [ ! -f $InputFile ];
		then
			Dummy="0"
			while [[ $Dummy != "1" ]];
			do
				echo -n "Please Enter A Valid Input File Again: "
				read NewFileName
			
				if [ -f $NewFileName ];
				then
					InputFile=$NewFileName
					Dummy="1"
				fi
			done
		fi
	elif(($#==2)); 
	then
		InputFile=$2
		WorkingDirectory=$1
		
		if [ ! -d $WorkingDirectory ];
		then
			echo "Please Enter A Valid Directory"
			exit -1
		fi
		
		if [ ! -f $InputFile ];
		then
			Dummy="0"
			while [[ $Dummy != "1" ]];
			do
				echo -n "Please Enter A Valid Input File Again: "
				read NewFileName
			
				if [ -f $NewFileName ];
				then
					InputFile=$NewFileName
					Dummy="1"
				fi
			done
		fi
	else
		echo 'Use the script like this: ./1705058.sh Working_Directory(Optional, without space) Input_File_Name'
		exit -1
	fi	
	
	mkdir -p $OutputDirectory
}

#A function which reads the input file and stores the types to ignore inside an array
GetIgnorableTypes(){
	Counter=0
	if [ -f $1 ];
	then
		for each in $(sed 's/\r$//g' $1)
		do
			Counter=$((Counter+1))
			Types[$Counter]=$each
		done
	fi
}

#A Function Which Finds all files recursively excluding the files mentioned
CreateFileCommand(){
	CommandToExecute="find $1 -type f"
	IgnoredList="find $1 -type f" #Necessary for taking the count of ignored files
	
	shift
	ExtensionList=("$@")
	NumberOfExtensions=${#ExtensionList[@]}
	
	for each in "${ExtensionList[@]}"
	do
		NewComm=' ! -name '''*.$each''''
		NewComm2=" -iname *.$each -o -type f"
		CommandToExecute="$CommandToExecute$NewComm"
		IgnoredList="$IgnoredList$NewComm2"
	done
	
	if((NumberOfExtensions!=0));
	then
		IgnoredList=${IgnoredList%???????????} #Trimming the extra " -o -type f" from the command
	fi
	
	#echo "$IgnoredList"
	#echo "$CommandToExecute"
}

#A Function That Counts The Numbers Of Files Ignored
GetIgnoredCount(){
	((IgnoredCount=0))
	Files=$($1)
	for each in $Files
	do
		((IgnoredCount++))
	done
	echo "ignored, $IgnoredCount" >> $2
}

#A function Which Creates Subfolder For Each File Types, the distributes the files
CreateSubfolders(){
	declare -A FileCount #A Hashmap to store the count of different file types
	Extension=$($1 | sed 's/.*\.//' | sort -u) #Gets the unique extensions of files by subtracting everything before a ".", leaving only the extension
	Extension=$(echo "$Extension" | sed 's/.*\/.*/others/' | sort -u) #If there is any file without extension, it will be put under "others" folder
	#echo "$Extension"
	
	AllDirectories=$($1)
	AllUniqueFiles=$($1 | sed 's/.*\///' | sort -u) #Subtracts everything before the last "/", leaving only the filename with extension
	
	for each in $Extension
	do
		mkdir -p "$2/$each" #Make Sub Folders
		touch "$2/$each/desc_$each.txt" #Make File Description Text
		>"$2/$each/desc_$each.txt" 
		((Counter=0))
		FileCount[$each]="$Counter"
	done
	
	
	local IFS=$'\n' #Setting local ISF to newline so that files with space in their names can be detected
	for file in $AllUniqueFiles #Looping through all the unique files
	do
		FileDirectory=""
		FileExtension=""
		Destination="output_dir"
		#echo "$file"
		
		local IFS=$'\n' #Setting local ISF to newline so that directories with space in their names can be detected
		for Directory in $AllDirectories #Loop to obtain the directory of the file
		do
			#echo "$Directory"		
			if [[ $Directory = *"$file"* ]];
			then
				FileDirectory="$Directory"
				break
			fi
		done
		unset IFS
		
		if [[ $file = *'.'* ]];
		then
			FileExtension="$(echo "$file" | sed 's/.*\.//')" #If file has extension
		else
			FileExtension="others" #If file does not have extension
		fi
		
		Destination="$Destination/$FileExtension"
		cp "$FileDirectory" $Destination #Copy File To Destination
		echo "$FileDirectory" >> "$Destination/desc_$FileExtension.txt" #Add file directory to description text file
		FileCount[$FileExtension]=$((FileCount[$FileExtension]+1))
	done
	unset IFS
	
	for each in ${!FileCount[@]} #Write the file counts in the csv
	do
		echo "$each, ${FileCount[$each]}" >> $3
	done
}

#A Function Which Creates And Writes To The Output CSV File
CreateOutputCSV(){
	touch "$1"
	>"$1"
	echo "file_type, no_of_file" >> "$1"
}

main(){
	#declare -A FileCount
	CSVName="output.csv"
	GetInput $*
	GetIgnorableTypes $InputFile
	CreateFileCommand $WorkingDirectory ${Types[@]}
	CreateOutputCSV $CSVName
	
	if [[ "$IgnoredList" = "find $WorkingDirectory -type f" ]]
	then
		echo "ignored, 0" >> $CSVName
	else
		GetIgnoredCount "$IgnoredList" "$CSVName"
	fi
	
	CreateSubfolders "$CommandToExecute" "$OutputDirectory" "$CSVName"
}

main $*
