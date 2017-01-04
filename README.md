# Sensors Network Model
<p align="justify">
This simulation model is the result of a didactic project for the
<a href="http://www.dis.uniroma1.it/~hpdcs/index.php?option=com_content&view=article&id=22">
Concurrent and Parallel Programming</a> course of the Master of Science of
Engineering in Computer Science at <a href="http://cclii.dis.uniroma1.it/?q=it/msecs">Sapienza University of Rome</a>.
<br>
The author of this project is <a href="https://www.linkedin.com/in/leonidavide">Davide Leoni</a>.
</p>
<h2>Goal and Motivations</h2>
<p align="justify">The goal of this project is to build a model to simulate the behaviour of <i>wireless sensor network (WSN)</i>: these networks consist of small devices featuring power source, a microprocessor, a wireless interface, some memory and one or more sensors. They are used to gather information from the sensors in a given location or region.
<br>Because of the limited radio communication range, nodes can communicate using <i>multi-hop routing protocols</i>
<br>Many alghorithms and protocols have been developed to collect data inside wireless sensors networks, and among these, the <i><b>Collection Tree Protocol (CTP)</b></i> is one of the most adopted for reaseach, teaching and commercial products. It has been already deployed in many real sensors networks and it has been implemented on different hardware platforms using various programming languages, from C to Java ( more about CTP is available <a href="http://sing.stanford.edu/gnawali/ctp/">here</a> ).
<br>This model provides a simulation of a WNS running the CTP and is meant to be run on the <a href="https://github.com/HPDCS/ROOT-Sim">ROme OpTimistic Simulator (ROOT-Sim)</a>.
</p>
<h2>Collection Tree Protocol</h2>
<h3>Overview</h3>
<p align="justify">
As already said, CTP is a <i>data collection</i> protocol for WNSs, so, just like the other protocols of such kind, it is based on (at least) one routing tree: the nodes are the devices featuring the sensors (that continuously collect physical data) and the links are the wireless 1-hop links among them; two nodes are neighbors if an edge exists between them.
<br>The root of the tree collects data packets that are forwarded by the other nodes of the tree. Each node forwards packets to its <i>parent</i>, chosen among its neighbor nodes. In order to make a choice, each node must be aware of the state of its neighbors: that's why nodes continuously broadcast special packets, called <i>beacons</i>, describing their condition.
<br> Data collection protocols differ in two aspects:
<ol>
<li>the <i>metric</i> used by the nodes to choose their parents</li>
<li>the capability of identifying and repairing critical situations like <i>routing loops</i></li>
</ol>
The main challenge for data collection protocols is represented by the fact that wireless links between nodes are not realiable; rather they are very instable.
</p>
<h3>Architecture</h3>
<p align="justify">
CTP consists of three main logical software components:
<ol>
<li><i><b>Link Estimator</b></i></li>
<li><i><b>Routing Engine</b></i></li>
<li><i><b>Forwarding Engine</b></i></li>
</ol>
<p align="center">
<img src="CTP_architecture.jpeg">
</p>
The above picture shows the software components of a node running the Collection Tree Protocol: arrows between components show that there's interaction between them.
<br><br>The metric adopted in CTP for the selection of the parent node is the <i><b>ETX (Expected Transmissions)</b></i>: a node whose ETX is equal to <i>n</i> can deliver a data packet to the root node with an average of <i>n</i> transmissions.
<br>The ETX of any node is recursively defined as the ETX of its parent plus the ETX of its link to the parent; the root node represents the base case in this recursion, and its ETX is obviously equal to 0.
<br>Packets flow in the collection tree from the nodes to the root according to the gradient represented by ETX, from the leaf nodes, having the highest value of ETX, to the root of the node, where the ETX is 0.  
The ETX associated to a link is referred to as <i>1-hop ETX</i> and is computed by the Link Estimator; the ETX associated to a neighbor is referred to as <i>multi-hop ETX</i> (or simply ETX) and is the sum of the ETX declared by the neighbor (through its beacons) and the 1-hop ETX of the link to the neighbor.
</p>
<h3>Link Estimator</h3>
<p align="justify">
This is in charge of computing the incoming and outgoing quality of the links, in order to evaluate their 1-hop ETX.
<br>In particular, the <i>incoming quality</i> is calculated as the number of beacons sent by a neighbor over the number of beacons received by the same neighbor.
<br> The <i>outgoing quality</i> is the ratio between the number of data packets forwarded to a neighbor and the number of acknowledgements sent by this.
<br> Analyzing the headers and footers of beacons sent and received by the node, the Link Estimator builds and updates a table, the <i>link estimator table</i>, where it stores the 1-hop ETX of each neighbor node.
</p>
<h3>Routing Engine</h3>
<p align="justify">
This component is dedicated to the selection of the <i>parent</i> node, i.e. the neighbor with the lowest value of the multi-hop ETX.
<br> Since the multi-hop ETX is the sum of the 1-hop ETX and the ETX declared by neighbors in their beacons, the Routing Engine has to maintain a table, called <i>routing table</i>, where it stores the last ETX value read in the beacons from each neighbor : in this way, it is able to always choose the "best" neighbor (the one with the lowest multi-hop ETX) as parent.
<br>On one hand, the Routing Engine continuously updates the table reading the information contained in the beacons received from the neighbors, on the other hand it writes the ETX of the current parent in the beacons to be sent to the neighbors.
<br>This component is tightly coupled with the Link Estimator, since it needs to know, for each neighbor, the 1-hop ETX of the corresponding link. Their respective tables are also connected. For example, if the entry corresponding to a neighbor is removed from the link estimator table (probably because it is no longer reachable), the Routing Engine is forced to remove the entry corresponding to the neighbor also from the routing table.
</p>
<h3>Forwarding Engine</h3>
<p align="justify">
By mean of the Forwarding Engine, a node performs three tasks:
<ol>
<li>Forwards data packets received by its neighbors to the node that it has selected as parent; this, on its turn, will forward the packets to its own parent, so they will be finally delivered to the root of the collection tree</li>
<li>Detects and tries to fix <i>routing loops</i> (two or more nodes that send packets among each other forever, in such a way that the root will never receive them).</li>
<li>Detects and drops duplicate packets; this is achieved thanks to a cache where it stores the most recently sent data packets: before forwarding a packet, it checks whether there is one identical in the cache and, if so, drops the packet</li>
</ol>
</p>
<h2>The Simulation Model</h2>
<h3>Overview</h3>
<p>There are many aspects that can be taken into account when developing a model of a WNS, depending on how accurate the simulation has to be. Some of them are:
<ol>
<li><b>connectivity</b> -> given a node, only some other nodes are capable of communicating with it because of the limited range of wireless connections; they are referred to as <i>neighbours</i></li>
<li><b>interferences</b> -> wireless links are not realiable because of physical characteristics of the physical medium, that suffer from phenomena like multipath propagation.Hence, a node may not correctly receive a message sent by a neighbor node</li>
<li><b>knowledge of the nodes</b> -> if nodes run a <i>global algorithm</i> they are full aware of the state of all the other nodes; if the algorithm is <i>distributed</i>, nodes only know a their own state when they start and they can learn about the state of their neighbours little by little by sending and receving packets.</li>
<li><b>communication modes</b> -> nodes may be capable of sending only <i>broadcast</i> messages (received by all nodes within the range of the wireless transceiver), only <i>unicast</i> messages (received by one designed recipient) or both of them.</li>
<li><b>latency of the network</b> -> usually there is some delay between the moment when a message is sent and the moment when it is received</li>
<li><b>distribution of the nodes</b> -> the position of the nodes in the 2-dimensional or 3-dimensional space may be known before the simulation starts or it may be completely random</li>
<li><b>identification of the nodes</b> -> unique identifiers (ID) may be associated to each node</li>
<li><b>reliability of nodes</b> -> real nodes can't run forever, so they are supposed to fail after some time. Their failure may also occur earlier because they run out of power supply or a random fail occurs</li>
</ol>

In a wireless sensors networks, each node essentially performs three tasks:
<ol>
<li>collection of samples from the physical world by mean of a sensor</li>
<li>transmission of the data collected to the other nodes of the network, according to some routing protocol</li>
<li>forwarding of data received from some nodes to other nodes of the network, according to some routing protocol</li>
</ol>
<b>The model proposed here is focused on the simulation of a specific routing protocol, the Collection Tree Protocol, rather than on the simulation of sensors sampling. As a consequence, data sampled from sensors are modelled as simple random values periodically produced by the nodes.
</b>
<br>
<br>
A node is modelled as one of the <i>Logical Processes (LPs)</i> created by ROOT-Sim, so it is associated with a top-level data structure (named <i>node_state</i>) that represents the state of the logical process during the simulation. Every instance of <i>node_state</i> contains the software components of the Collection Tree Protocol
</p>
<h3>Measures Table</h3>
<p align="justify">
Every time a message is received, values of acceleration, ID of the producer and a timestamp are uploaded to a online repository: <a href="http://www.parse.com">Parse</a> was chosen as repository,
mostly because it's free and APIs are really easy to use (they are simple REST calls). Here's a view of the dashboard from which the user can manage its custom repository
<p align="center">
<br><br>
<img src="/Images/Parse.jpeg">
</p>
It's possible to retrieve at any moment any number of the last measures uploaded on Parse; also it is possible to clean up the table.
</p>
<p align="center">
<br><br>
<img src="/Images/MeasuresTable.jpeg">
</p>
<h3>Paths Table and Graph</h3>
<p align="justify">
The main part of the whole application is the graphical representation of the sensors network, connected to the Paths Table. All the motes are drawn using an
icon (as regards with producers, they have also a red circle around it) and the links among them are drawn with different colors depending on the rows selected
in the adjacent Paths Table. Here there's one row for each producer mote, with the indication of the number of forwarders that processed the
last message it sent (this value gets updated every time a new message from the same mote is received). Also the timestamp of the last
message received by each producer is reported. Moreover, a link is drawn between the icon representing the mote and another icon representing the host running
the Java application. Finally, all the links are labeled with their position within the selected path: for example, if there was a path 1->2->3->4, the link 1->2
would have index equal to 1, the link 2->3 would have index equal to 2, and so on and so forth.
</p>
<p align="center">
<br><br>
<img src="/Images/PathsTable.jpeg">
</p>
<p align="center">
<br><br>
<img src="/Images/Canvas.jpeg">
</p>
<h2>Final notes</h2>
<p align="justify">
<ol>
<li>the "Data Collection" module was developed and tested on TinyOs release 2.1.2</li>
<li>the "Data visualization and storage" module depends on SDK of TinyOs release 2.1.2. As regards with the upload of data to Parse, an
<a href="https://hc.apache.org/httpcomponents-client-4.5.x/index.html">HTTP client by Apache</a> (release 4.5) was used to make REST request;
furthermore a <a href="https://mvnrepository.com/artifact/org.json/json">JSON library</a> was used to parse the response from Parse.</li>
</ol>
</p>
<h3>Installation instructions</h3>
<p align="justify">
In order to successfully deploy the wireless sensors network, a release of TinyOs and tool capable of reading Makefiles are mandatory.
Also the two libraries cited above (HTTP Client and JSON library) have to be downloaded and their paths have to be included in the CLASSPATH environment variable.
Here's the tasks to install the code into the motes:
<ol>
<li><i>producers</i>: please write to me to get the nesC code necessary to control the Magonode platforms (see "Hardware implementation" above).
Once this code is available, it's necessary to browse to the folder "Producers", open a terminal window a execute the command</li>
<br>
<br>
<i>make magonode install</i>
<br>
<br>
<li><i>forwarders</i>: browse to the folder "Forwarders", open a terminal window a execute the command</li>
<br>
<br>
<i>make telosb install</i>
<br>
<br>
</ol>
The "Makefile" given for producers, not only compiles the nesC code to drive the motes, but also generates the java class that is going to be used
by the Java application to parse messages received by motes (SensorsDataMsg): it depends on the <a href="http://manpages.ubuntu.com/manpages/xenial/man1/mig.1.html">mig tool</a>
to create the class. When the make file has completed its execution, the newly generated java class will be available directly inside the folder "mviz" of this project.
Last step to perform consists in replacing the content of the "mviz" folder inside the own TinyOs release with the content of the "mviz" folder which is part of this solution
(checking that the file "SensorsDataMsg.java" is included). At this point the Java application can be run simply executing the script "tos-mviz" (part of the TinyOs release)
from the folder containing the file "config.properties"
<h3>Customization</h3>
<p align="justify">
This solution features two files that can be used to customize the tool, both because of specific user needs, or for a different configuration of the enviroment where
the tool is executed or simply to test different it with different configurations:
<ol type="1">
<li><i>"Network.h"</i>: with this header it is possible to tune most of parameters of the sensors network, like the ID of the root mote, the frequency of sampling of the accelerometer,
the depth of the messages queues of the motes etc.</li>
<li><i>"config.properties"</i>: this configuration file contains path to the icons used to draw motes in the Java application, preferred size of its window etc.
IMPORTANT!!! Also the keys to use the Parse API have to be specified: contact me to get them</li>
</ol>
</p>
