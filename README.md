# Chat Program

---

## Prerequisites

---

- Git
- Bash
- Clang & GCC compilers
- POSIX based system(s)

## Cloning the Repository

---

Clone the repository using the following command:
````
git clone https://github.com/jamesnll/C-Chat-Program.git
````
Navigate to the cloned directory:
````
cd C-Chat-Program
````

## Running the Script Files

---

Ensure that all the script files are executable:
````
chmod u+x generate-flags.sh
chmod u+x generate-cmakelists.sh
chmod u+x change-compiler.sh
chmod u+x build.sh
````

Execute the scripts in order:
````
./generate-flags.sh
````
````
./generate-cmakelists.sh
````
Depending on what compiler is used, substitute 'compiler' for gcc or clang 
````
./change-compiler -c 'compiler'
````
````
./build.sh
````
Finally, change directory:
````
cd build
````

## Executing the Program

---
To run the program, execute the following commands:

First establish the connection
````
./chat -a 'ip address' 'port'
````
Then connect to the host:
````
./chat -c 'ip address' 'port'
````

Now both users will be able to send and receive messages from one another by typing into the console.

## Closing the Program

---

To close the connection, either user can press ctrl + z.
