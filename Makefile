udptcpbridge:	udptcpbridge.o
	gcc -o udptcpbridge udptcpbridge.o -pthread -lstdc++

%.o:	%.cpp
	g++ -c -o $@ $<

clean:	
	rm *.o udptcpbridge
