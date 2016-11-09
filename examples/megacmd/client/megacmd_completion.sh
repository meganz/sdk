_megacmd()
{
	local cur opts
	COMPREPLY=()
	cur="${COMP_WORDS[COMP_CWORD]}"

	if [[ ${cur} == '=' ]]; then
		cur=""
	fi
	
	COMP_WORDS[0]="${COMP_WORDS[0]/mega-/}"
	opts="$(mega-exec completion ${COMP_LINE/#mega-/})"

	COMPREPLY=( $(compgen -W "${opts}" -- ${cur}) )

	for i in "${COMPREPLY[@]}"; do
		if [[ ${i} == --*= ]] || [[ ${i} == */ ]]; then
			compopt -o nospace
		fi
	done
	
	return 0
}
for i in $(compgen -ca | grep mega-); do
	complete -F _megacmd $i
done
