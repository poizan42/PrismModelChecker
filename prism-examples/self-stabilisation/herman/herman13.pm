// herman's self stabilising algorithm [Her90]
// gxn/dxp 13/07/02

// the procotol is synchronous with no non-determinism (a DTMC)
dtmc

// module for process 1
module process1

	// Boolean variable for process 1
	x1 : [0..1];
	
	[step]  (x1=x13) -> 0.5 : (x1'=0) + 0.5 : (x1'=1);
	[step] !(x1=x13) -> (x1'=x13);
	
endmodule

// add further processes through renaming
module process2 = process1[x1=x2, x13=x1 ] endmodule
module process3 = process1[x1=x3, x13=x2 ] endmodule
module process4 = process1[x1=x4, x13=x3 ] endmodule
module process5 = process1[x1=x5, x13=x4 ] endmodule
module process6 = process1[x1=x6, x13=x5 ] endmodule
module process7 = process1[x1=x7, x13=x6 ] endmodule
module process8 = process1[x1=x8, x13=x7 ] endmodule
module process9 = process1[x1=x9, x13=x8 ] endmodule
module process10 = process1[x1=x10, x13=x9 ] endmodule
module process11 = process1[x1=x11, x13=x10 ] endmodule
module process12 = process1[x1=x12, x13=x11 ] endmodule
module process13 = process1[x1=x13, x13=x12 ] endmodule

// cost - 1 in each state (expected steps)
rewards
	
	true : 1;
	
endrewards

// any initial state (consider any possible initial configuration of tokens)
init
	
	true
	
endinit
