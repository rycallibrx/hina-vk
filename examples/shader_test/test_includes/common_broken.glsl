// Common header with intentional error
// This error should be reported with THIS filename

struct BrokenStruct {
    vec4 position   // Missing semicolon - ERROR!
    vec4 color;
};
