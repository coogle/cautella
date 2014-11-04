cautella
========

Cautella is a PHP extension I wrote forever ago, and it's purpose was to fundamentally attach itself to the output mechanism
of PHP and allow a developer to alter or completely silence a particular output based on logic. 

It functions by callback and by hooking directly into the opcode handlers for print and echo statements, making it ideal
for plugging into PHP applications where you have no control over the output they generate. 

I will be honest, I have absolutely no idea why I wrote this. Could be useful for security purposes.

See foo.php in the source tree for an example. 
