Wiring tutorial on Unitree Go2
===
> [!IMPORTANT]
> Remove the battery from the robot before opening it.

  
Unscrew the two screws of the handle.   

<div align="center">
<img src="images/Go2/1.jpg" alt="The 2 screws of the handle marked in red" width="600"/>
</div>
<br>

Remove the 4 screws that holds the Jetson.

<div align="center">
<img src="images/Go2/2.jpg" alt="The 4 screws of the jetson marked in red" width="600"/>
</div>
<br>

Next, unplug the power and ethernet wire connecting the robot to the Jetson and put the Jetson aside.   
<div align="center">
<img src="images/Go2/3.jpg" alt="The  wires to unplug marked in red" width="600"/>
</div>
<br>

To remove the top plastic shell of the robot and access the mother board, remove 4 screws: two on the top and two on the back, near the legs actuators.    
<table>
    <td>
        <div align="center">
            <img src="images/Go2/4.1.jpg" alt="Localisation of the top screws" width="600"/>
        </div>
    </td>
        <td>
        <div align="center">
            <img src="images/Go2/4.2.jpg" alt="Localisation of the back screws" width="600"/>
        </div>
    </td>
</table>
<br>

Remove the cover.

<div align="center">
<img src="images/Go2/5.jpg" alt="The robot with the cover opened" width="600"/>
</div>
<br>

Solder two wires directly to the mother board on the emegency pads (in red). And thread them through the hole in the top pannel that is used for the ethernet and the power for the Jetson.   

<div align="center">
<img src="images/Go2/6.jpg" alt="The robot motherboard" width="600"/>
</div>
<br>

You can connect the other end of the wire (the part that will be outside of the robot) directly to the receiver module or to any connector of your liking.  
We use a female 5.5 x 2.1mm Power Jack connector plugged to a male connector of the same type soldered to the receiver.