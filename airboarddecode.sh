grep 'marking.*pressed at' /tmp/airboard.log |
    awk '{print }' |
    sed -e 's/key_//' 
	-e 's;slash;/;' 
	-e 's/comma/,/' 
	-e 's/period/./' 
	-e 's/space/ /' 
	-e 's/minus/-/' 
	-e 's/capcontrol/ctrl/'  |
    more
