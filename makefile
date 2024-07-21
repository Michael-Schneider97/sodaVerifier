compiler = g++
src = sodaController.cpp
target = sodaController
flags = -lpigpio -Irt -pthread -IZXing -std=c++17
# don't forget -o

# clean up object files
clean: 
	rm -f .o*

cleanex:
	rm -f $(target)

# this wont work unless you correctly setup git
update: clean, cleanex
	git pull
	$(compiler) $(src) -o $(target) $(flags)

