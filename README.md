# Virtuoso: An Open-Source, Comprehensive and Modular Simulation Framework for Virtual Memory Research


This is an alpha version of the simulator. Maintainance and updates will keep coming. Stay tuned !

**Structure of the repo:**

1. **Software Requirements**
   - Container images and included software.
   
2. **Software Requirements for Containerized Execution**
   - Software and installation instructions.
   
3. **Simulation Structure**
   - New components and modularity
  
## Software requirements


We prepared container images which are uploaded publicly in Docker hub
under the tags:  

``` cpp
#Contains all the simulator dependencies 
1. kanell21/artifact_evaluation:victima                   
```

To install Docker/podman execute the following script:
``` bash
kanellok@safari:~/virtuoso$ sh install_container.sh docker
or 
kanellok@safari:~/virtuoso$ sh install_container.sh podman

```

You need to cd back to the cloned repository to refresh the permissions since we executed:
``` bash
su - $USER 
```

Using container images, you can easily use the simulator without installing multiple new packages and handling cyclic dependencies. 

## Getting Started

Virtuoso is built on top of Sniper but can be plugged into multiple simulators.  
We refer the users to Sniper's [website](https://snipersim.org/w/Getting_Started) and manual for more information about the underlying simulator. 

## New components and techniques supported by Virtuoso

![Overview](https://github.com/CMU-SAFARI/Virtuoso/blob/main/virtuoso_components.png)



## Contact 

Konstantinos Kanellopoulos <konkanello@gmail.com>  
Konstantinos Sgouras <sgouraskon@gmail.com>







   
