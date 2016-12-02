
_megacmd()
{
	local cur opts
	COMPREPLY=()
	cur="${COMP_WORDS[COMP_CWORD]}"

	if [[ ${cur} == '=' ]]; then
		cur=""
	fi
	
	COMP_WORDS[0]="${COMP_WORDS[0]/mega-/}"
	linetoexec=""
	lasta=""
	for a in "${COMP_WORDS[@]}"; do
		if [[ $a == *" "* ]]
			then
				linetoexec=$linetoexec" \""$(echo $a | sed 's#\\$#\\ #g' | sed 's#\\\ # #g')"\""
			else
				if [[ ${a} == '=' ]] || [[ ${lasta} == '=' ]] || [[ ${a} == ':' ]] || [[ ${lasta} == ':' ]]; then
					linetoexec=$linetoexec$a
				else
					linetoexec=$linetoexec" "$(echo $a | sed 's#\\$#\\ #g' | sed 's#\\\ # #g')
				fi
			fi
		lasta=$a
	done
        if [[ "$linetoexec" == *" " ]]
        then
            linetoexec="$linetoexec\"\""
        fi
	opts="$(mega-exec completion ${linetoexec/#mega-/} 2>/dev/null)"
	if [ $? -ne 0 ]; then
		return $?
	fi

	declare -a "aOPTS=($opts)"

	for a in `seq 0 $(( ${#aOPTS[@]} -1 ))`; do
		COMPREPLY[$a]=$( echo ${aOPTS[$a]} | sed "s# #\\\ #g")
	done

	for i in "${COMPREPLY[@]}"; do
		if [[ ${i} == --*= ]] || [[ ${i} == */ ]]; then
			hash compopt 2>/dev/null >/dev/null && compopt -o nospace
		fi
	done
	
	if [[ $opts == "MEGACMD_USE_LOCAL_COMPLETION" ]]; then
		COMPREPLY=()
	fi
	
	return 0
}
for i in $(compgen -ca | grep mega-); do
	IFS=" " complete -o default -F _megacmd $i
done
