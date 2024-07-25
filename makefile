compiler = g++
src = sodaController.cpp
target = sodaController
flags = -lpigpio -Irt -pthread -lZXing -std=c++17
# don't forget -o

            

# clean up object files
.PHONY: clean
clean: 
	rm -f .o*

cleanex:
	rm -f $(target)
	
build:
	$(compiler) $(src) -o $(target) $(flags)

# this wont work unless you correctly setup git
update: clean cleanex
	git pull
	$(compiler) $(src) -o $(target) $(flags)

