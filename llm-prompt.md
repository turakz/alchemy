*"I want to write a C++ command-line tool that analyzes C source files to extract all struct definitions and their fields. The tool should be able to:

Compute and display the current memory layout of each struct, including size, padding, and alignment.

Suggest an optimized field ordering to reduce padding and show the resulting memory layout and size.

Provide efficiency analysis, like memory savings from the optimized layout.

Give cache-awareness insights, such as how many structs fit per cache line and recommended array sizes for better caching.

Is this feasible to do in C++? What would be the main challenges, and what libraries or tools would make this realistic? Can you suggest an architecture or roadmap for building this tool?"*
