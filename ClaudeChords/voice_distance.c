/*
 * voice_distance~ - Pure Data external for calculating voice leading distance matrices
 * CORRECTED VERSION - Fixed multiple bugs
 */

#include "m_pd.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_VOICES 8

static t_class *voice_distance_class;

typedef struct _voice_distance {
    t_object x_obj;
    t_float *x_current_chord;     
    t_float *x_target_chord;      
    t_int x_current_size;         
    t_int x_target_size;          
    t_int x_matrix_size;          
    t_int x_has_current;          // Flag: do we have current chord?
    t_int x_has_target;           // Flag: do we have target chord?
    t_outlet *x_outlet;           
    t_outlet *x_size_outlet;      
} t_voice_distance;

// Function prototypes
static void voice_distance_list(t_voice_distance *x, t_symbol *s, int argc, t_atom *argv);
static void voice_distance_target(t_voice_distance *x, t_symbol *s, int argc, t_atom *argv);
static void voice_distance_calculate(t_voice_distance *x);
static t_float semitone_distance(t_float note1, t_float note2);
static void voice_distance_debug(t_voice_distance *x);
static void voice_distance_clear(t_voice_distance *x);

// Constructor
static void *voice_distance_new(void) {
    t_voice_distance *x = (t_voice_distance *)pd_new(voice_distance_class);
    
    // Allocate memory for chord arrays
    x->x_current_chord = (t_float *)getbytes(MAX_VOICES * sizeof(t_float));
    x->x_target_chord = (t_float *)getbytes(MAX_VOICES * sizeof(t_float));
    
    // Initialize state
    x->x_current_size = 0;
    x->x_target_size = 0;
    x->x_matrix_size = 0;
    x->x_has_current = 0;
    x->x_has_target = 0;
    
    // Initialize arrays to zero
    memset(x->x_current_chord, 0, MAX_VOICES * sizeof(t_float));
    memset(x->x_target_chord, 0, MAX_VOICES * sizeof(t_float));
    
    // Create inlet for target chord (FIXED: proper inlet creation)
    inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_list, gensym("target"));
    
    // Create outlets  
    x->x_outlet = outlet_new(&x->x_obj, &s_list);
    x->x_size_outlet = outlet_new(&x->x_obj, &s_float);
    
    return (void *)x;
}

// Destructor
static void voice_distance_free(t_voice_distance *x) {
    if (x->x_current_chord) {
        freebytes(x->x_current_chord, MAX_VOICES * sizeof(t_float));
    }
    if (x->x_target_chord) {
        freebytes(x->x_target_chord, MAX_VOICES * sizeof(t_float));
    }
}

// Handle current chord input (left inlet)
static void voice_distance_list(t_voice_distance *x, t_symbol *s, int argc, t_atom *argv) {
    // FIXED: Better input validation
    if (argc <= 0) {
        pd_error(x, "voice_distance: empty chord received");
        return;
    }
    
    if (argc > MAX_VOICES) {
        pd_error(x, "voice_distance: too many voices (%d), max is %d", argc, MAX_VOICES);
        return;
    }
    
    // Store current chord
    x->x_current_size = argc;
    for (int i = 0; i < argc; i++) {
        x->x_current_chord[i] = atom_getfloat(argv + i);
    }
    x->x_has_current = 1;
    
    post("voice_distance: current chord received (%d voices)", argc);
    
    // Calculate if we have both chords
    if (x->x_has_target) {
        voice_distance_calculate(x);
    } else {
        post("voice_distance: waiting for target chord...");
    }
}

// Handle target chord input (right inlet)
static void voice_distance_target(t_voice_distance *x, t_symbol *s, int argc, t_atom *argv) {
    // FIXED: Better input validation
    if (argc <= 0) {
        pd_error(x, "voice_distance: empty target chord received");
        return;
    }
    
    if (argc > MAX_VOICES) {
        pd_error(x, "voice_distance: too many target voices (%d), max is %d", argc, MAX_VOICES);
        return;
    }
    
    // Store target chord
    x->x_target_size = argc;
    for (int i = 0; i < argc; i++) {
        x->x_target_chord[i] = atom_getfloat(argv + i);
    }
    x->x_has_target = 1;
    
    post("voice_distance: target chord received (%d voices)", argc);
    
    // Calculate if we have both chords
    if (x->x_has_current) {
        voice_distance_calculate(x);
    } else {
        post("voice_distance: waiting for current chord...");
    }
}

// Calculate distance matrix
static void voice_distance_calculate(t_voice_distance *x) {
    // FIXED: Check that we actually have both chords
    if (!x->x_has_current || !x->x_has_target) {
        pd_error(x, "voice_distance: missing chord data for calculation");
        return;
    }
    
    if (x->x_current_size <= 0 || x->x_target_size <= 0) {
        pd_error(x, "voice_distance: invalid chord sizes (%ld, %ld)", 
                 x->x_current_size, x->x_target_size);
        return;
    }
    
    // FIXED: Matrix should be sized to accommodate both chords properly
    x->x_matrix_size = (x->x_current_size > x->x_target_size) ? 
                       x->x_current_size : x->x_target_size;
    
    int matrix_elements = x->x_matrix_size * x->x_matrix_size;
    t_atom *output = (t_atom *)getbytes(matrix_elements * sizeof(t_atom));
    
    if (!output) {
        pd_error(x, "voice_distance: memory allocation failed");
        return;
    }
    
    post("voice_distance: calculating %dx%d distance matrix", 
         x->x_matrix_size, x->x_matrix_size);
    
    // FIXED: Proper matrix calculation with better penalty handling
    for (int row = 0; row < x->x_matrix_size; row++) {
        for (int col = 0; col < x->x_matrix_size; col++) {
            t_float distance;
            
            if (row >= x->x_current_size) {
                // No more current voices - high penalty
                distance = 1000.0;
            } else if (col >= x->x_target_size) {
                // No more target voices - high penalty  
                distance = 1000.0;
            } else {
                // Calculate actual semitone distance
                distance = semitone_distance(x->x_current_chord[row], 
                                           x->x_target_chord[col]);
            }
            
            SETFLOAT(output + (row * x->x_matrix_size + col), distance);
        }
    }
    
    // FIXED: Better debug output with bounds checking
    post("voice_distance: matrix calculated:");
    for (int row = 0; row < x->x_matrix_size && row < 6; row++) {  // Limit debug output
        char debug_line[256];
        char temp[32];
        strcpy(debug_line, "  row ");
        sprintf(temp, "%d: ", row);
        strcat(debug_line, temp);
        
        for (int col = 0; col < x->x_matrix_size && col < 8; col++) {  // Limit columns
            sprintf(temp, "%.1f ", atom_getfloat(output + row * x->x_matrix_size + col));
            strcat(debug_line, temp);
        }
        post("%s", debug_line);
    }
    
    // Output matrix size first
    outlet_float(x->x_size_outlet, (t_float)x->x_matrix_size);
    
    // Output distance matrix as flat list  
    outlet_list(x->x_outlet, &s_list, matrix_elements, output);
    
    // FIXED: Always free allocated memory
    freebytes(output, matrix_elements * sizeof(t_atom));
}

// FIXED: More sophisticated distance calculation
static t_float semitone_distance(t_float note1, t_float note2) {
    // Basic semitone distance
    t_float raw_distance = fabs(note2 - note1);
    
    // FIXED: Add bounds checking for reasonable MIDI note range
    if (note1 < 0 || note1 > 127 || note2 < 0 || note2 > 127) {
        post("voice_distance: warning - note outside MIDI range (%.1f, %.1f)", note1, note2);
    }
    
    // Optional: Consider octave equivalence for voice leading
    // For voice leading, we might want to consider octave wrapping
    // Uncomment for more sophisticated voice leading:
    /*
    if (raw_distance > 6.0) {
        t_float octave_up = fabs((note2 + 12.0) - note1);
        t_float octave_down = fabs((note2 - 12.0) - note1);
        raw_distance = fmin(raw_distance, fmin(octave_up, octave_down));
    }
    */
    
    return raw_distance;
}

// Clear stored chords
static void voice_distance_clear(t_voice_distance *x) {
    x->x_current_size = 0;
    x->x_target_size = 0;
    x->x_has_current = 0;
    x->x_has_target = 0;
    x->x_matrix_size = 0;
    
    memset(x->x_current_chord, 0, MAX_VOICES * sizeof(t_float));
    memset(x->x_target_chord, 0, MAX_VOICES * sizeof(t_float));
    
    post("voice_distance: cleared all chord data");
}

// FIXED: Enhanced debug function
static void voice_distance_debug(t_voice_distance *x) {
    post("voice_distance debug:");
    post("  has_current: %d, has_target: %d", x->x_has_current, x->x_has_target);
    post("  current chord (%d voices):", x->x_current_size);
    for (int i = 0; i < x->x_current_size && i < MAX_VOICES; i++) {
        post("    voice %d: %.1f (MIDI note %.0f)", i, x->x_current_chord[i], x->x_current_chord[i]);
    }
    post("  target chord (%d voices):", x->x_target_size);
    for (int i = 0; i < x->x_target_size && i < MAX_VOICES; i++) {
        post("    voice %d: %.1f (MIDI note %.0f)", i, x->x_target_chord[i], x->x_target_chord[i]);
    }
    post("  matrix size: %d", x->x_matrix_size);
}

// FIXED: Proper setup function
void voice_distance_setup(void) {
    voice_distance_class = class_new(gensym("voice_distance"),
                                   (t_newmethod)voice_distance_new,
                                   (t_method)voice_distance_free,
                                   sizeof(t_voice_distance),
                                   CLASS_DEFAULT,
                                   0);  // FIXED: No arguments in constructor
    
    // FIXED: Use class_addlist for main inlet
    class_addlist(voice_distance_class, voice_distance_list);
    
    // FIXED: Proper method registration for target inlet
    class_addmethod(voice_distance_class, (t_method)voice_distance_target,
                   gensym("target"), A_GIMME, 0);
    
    // Add utility methods
    class_addmethod(voice_distance_class, (t_method)voice_distance_debug,
                   gensym("debug"), 0);
    class_addmethod(voice_distance_class, (t_method)voice_distance_clear,
                   gensym("clear"), 0);
    
    class_sethelpsymbol(voice_distance_class, gensym("voice_distance"));
}