// Enhanced Orbifold-based voice leading with PD integration
#include "m_pd.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_VOICES 4
#define MAX_VOICING_VARIANTS 8
#define MIN_OCTAVE 2
#define MAX_OCTAVE 4
#define HIGH_COST 1000

static t_class *orbifold_class;

typedef struct _orbifold {
    t_object x_obj;
    t_inlet *x_in_root;
    t_inlet *x_in_chord;
    t_outlet *x_out_root;
    t_outlet *x_out_chord;
    t_outlet *x_out_cost;
    t_outlet *x_out_info;
    
    int current_chord[MAX_VOICES];
    int current_size;
    int root_interval;                    
    int chord_intervals[MAX_VOICES];      
    int chord_size;
    int target_intervals[MAX_VOICES];     
    int target_size;
    int feedback_enabled;
    int debug_enabled;
} t_orbifold;

// Forward declarations for methods
static void orbifold_list(t_orbifold *x, t_symbol *s, int argc, t_atom *argv);
static void orbifold_chord(t_orbifold *x, t_symbol *s, int argc, t_atom *argv);
static void orbifold_root(t_orbifold *x, t_floatarg f);
static void orbifold_calculate(t_orbifold *x);
static void orbifold_debug(t_orbifold *x, t_floatarg f);

// Constructor
static void *orbifold_new(t_floatarg f) {
    t_orbifold *x = (t_orbifold *)pd_new(orbifold_class);
    
    // Create additional inlets
    x->x_in_root = inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("root"));
    x->x_in_chord = inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("list"), gensym("chord"));
    
    // Create outlets (right to left)
    x->x_out_info = outlet_new(&x->x_obj, &s_list);
    x->x_out_cost = outlet_new(&x->x_obj, &s_float);
    x->x_out_chord = outlet_new(&x->x_obj, &s_list);
    x->x_out_root = outlet_new(&x->x_obj, &s_float);
    
    // Initialize state
    x->current_size = 0;
    x->chord_size = 0;
    x->root_interval = 0;
    x->feedback_enabled = 1;
    x->debug_enabled = 0;
    
    memset(x->current_chord, 0, MAX_VOICES * sizeof(int));
    memset(x->chord_intervals, 0, MAX_VOICES * sizeof(int));
    memset(x->target_intervals, 0, MAX_VOICES * sizeof(int));
    
    return (void *)x;
}

// Destructor
static void orbifold_free(t_orbifold *x) {
    // Free inlets (except main inlet)
    inlet_free(x->x_in_root);
    inlet_free(x->x_in_chord);
    
    // Free outlets
    outlet_free(x->x_out_info);
    outlet_free(x->x_out_cost);
    outlet_free(x->x_out_chord);
    outlet_free(x->x_out_root);
}

// Handle list input (hot inlet - current chord)
static void orbifold_list(t_orbifold *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        pd_error(x, "orbifold: too many voices (%d), max is %d", argc, MAX_VOICES);
        return;
    }
    
    x->current_size = argc;
    for (int i = 0; i < argc; i++) {
        x->current_chord[i] = (int)atom_getfloat(&argv[i]);
    }
    
    if (x->debug_enabled) {
        post("orbifold: received current chord (%d voices)", argc);
    }
    
    // Hot inlet - calculate immediately if we have chord data
    if (x->chord_size > 0) {
        orbifold_calculate(x);
    }

}

// Handle chord intervals (cold inlet)
static void orbifold_chord(t_orbifold *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        pd_error(x, "orbifold: too many intervals (%d), max is %d", argc, MAX_VOICES);
        return;
    }
    
    x->chord_size = argc;
    for (int i = 0; i < argc; i++) {
        x->chord_intervals[i] = (int)atom_getfloat(&argv[i]);
    }
    
    if (x->debug_enabled) {
        post("orbifold: stored chord intervals (%d voices)", argc);
    }
    // Cold inlet - don't calculate
}

// Handle root input (cold inlet)
static void orbifold_root(t_orbifold *x, t_floatarg f) {
    int root = (int)f;
    x->root_interval = root;
    
    if (x->debug_enabled) {
        post("orbifold: stored root %d", x->root_interval);
    }
    // Cold inlet - don't calculate
}

// Normalize chord to orbifold space
static void normalize_to_orbifold(t_orbifold *x, int *chord, int size, int *result) {
    if (x->debug_enabled) {
        post("DEBUG: Normalizing chord to orbifold space");
        post("DEBUG: Input chord: [%d %d %d %d]", 
             size > 0 ? chord[0] : -1,
             size > 1 ? chord[1] : -1,
             size > 2 ? chord[2] : -1,
             size > 3 ? chord[3] : -1);
    }
    
    // Convert to pitch classes
    int pc_chord[MAX_VOICES];
    for (int i = 0; i < size; i++) {
        pc_chord[i] = chord[i] % 12;
    }
    
    // Sort for canonical ordering
    for (int i = 0; i < size - 1; i++) {
        for (int j = i + 1; j < size; j++) {
            if (pc_chord[j] < pc_chord[i]) {
                int temp = pc_chord[i];
                pc_chord[i] = pc_chord[j];
                pc_chord[j] = temp;
            }
        }
    }
    
    // Normalize to start at 0
    int base = pc_chord[0];
    for (int i = 0; i < size; i++) {
        result[i] = (pc_chord[i] - base + 12) % 12;
    }
    
    if (x->debug_enabled) {
        post("DEBUG: Normalized orbifold result: [%d %d %d %d]",
             size > 0 ? result[0] : -1,
             size > 1 ? result[1] : -1,
             size > 2 ? result[2] : -1,
             size > 3 ? result[3] : -1);
    }
}

// Calculate geometric coordinates for tetrahedral space (4 voices)
static void geometric_coordinates(t_orbifold *x, int *chord, int size, float *coords) {
    if (size != 4) return;
    
    int normalized[MAX_VOICES];
    normalize_to_orbifold(x, chord, size, normalized);
    
    // Convert to geometric coordinates
    coords[0] = (float)(normalized[1] - normalized[0]) / 12.0f;  // x: spacing between voices 1-2
    coords[1] = (float)(normalized[2] - normalized[1]) / 12.0f;  // y: spacing between voices 2-3
    coords[2] = (float)(normalized[3] - normalized[2]) / 12.0f;  // z: spacing between voices 3-4
    
    if (x->debug_enabled) {
        post("DEBUG: Geometric coordinates: (%.3f, %.3f, %.3f)",
             coords[0], coords[1], coords[2]);
    }
}

// Calculate minimal voice leading distance considering all permutations
static float orbifold_distance(t_orbifold *x, int *chord1, int *chord2, int size, int *best_mapping) {
    float min_distance = HIGH_COST;
    int current_mapping[MAX_VOICES];
    int best_perm[MAX_VOICES];
    
    // Generate all permutations of target chord
    // For 4 voices, we'll do this manually for efficiency
    int permutations[24][4] = {
        {0,1,2,3}, {0,1,3,2}, {0,2,1,3}, {0,2,3,1},
        {0,3,1,2}, {0,3,2,1}, {1,0,2,3}, {1,0,3,2},
        {1,2,0,3}, {1,2,3,0}, {1,3,0,2}, {1,3,2,0},
        {2,0,1,3}, {2,0,3,1}, {2,1,0,3}, {2,1,3,0},
        {2,3,0,1}, {2,3,1,0}, {3,0,1,2}, {3,0,2,1},
        {3,1,0,2}, {3,1,2,0}, {3,2,0,1}, {3,2,1,0}
    };
    
    // Try all permutations
    for (int p = 0; p < 24; p++) {
        float total_distance = 0;
        int permuted_chord[MAX_VOICES];
        
        // Apply permutation
        for (int i = 0; i < size; i++) {
            permuted_chord[i] = chord2[permutations[p][i]];
            current_mapping[i] = permutations[p][i];
        }
        
        // Calculate voice leading distance
        for (int i = 0; i < size; i++) {
            float dist = fabs(permuted_chord[i] - chord1[i]);
            // Consider octave equivalence
            float oct_dist = fabs(dist - 12);
            total_distance += fmin(dist, oct_dist);
        }
        
        if (total_distance < min_distance) {
            min_distance = total_distance;
            memcpy(best_perm, current_mapping, size * sizeof(int));
        }
    }
    
    // Store best mapping
    memcpy(best_mapping, best_perm, size * sizeof(int));
    
    if (x->debug_enabled) {
        post("DEBUG: Best orbifold distance: %.2f", min_distance);
        post("DEBUG: Best voice mapping: [%d %d %d %d]",
             best_mapping[0], best_mapping[1], 
             best_mapping[2], best_mapping[3]);
    }
    
    return min_distance;
}

// Main calculation using orbifold methodology
static void orbifold_calculate(t_orbifold *x) {
    if (x->current_size == 0 || x->chord_size == 0) {
        post("orbifold: missing chord data (current:%d, chord:%d)", 
             x->current_size, x->chord_size);
        return;
    }
    
    if (x->debug_enabled) {
        post("\nDEBUG: Starting orbifold calculation");
        post("DEBUG: Current chord: [%d %d %d %d]",
             x->current_chord[0], x->current_chord[1],
             x->current_chord[2], x->current_chord[3]);
    }
    
    // Build target chord from intervals
    int target_chord[MAX_VOICES];
    int base_note = x->current_chord[0];  // Use current bass note as reference
    
    for (int i = 0; i < x->chord_size; i++) {
        target_chord[i] = base_note + x->chord_intervals[i];
    }
    
    // Get geometric coordinates for both chords
    float current_coords[3], target_coords[3];
    geometric_coordinates(x, x->current_chord, x->current_size, current_coords);
    geometric_coordinates(x, target_chord, x->chord_size, target_coords);
    
    // Find optimal voice mapping
    int best_mapping[MAX_VOICES];
    float min_distance = orbifold_distance(x, x->current_chord, target_chord, 
                                         x->current_size, best_mapping);
    
    // Apply optimal mapping to create output
    t_atom output_list[MAX_VOICES];
    for (int i = 0; i < x->current_size; i++) {
        SETFLOAT(&output_list[i], target_chord[best_mapping[i]]);
    }
    
    // Output results
    outlet_float(x->x_out_cost, min_distance);
    outlet_list(x->x_out_chord, &s_list, x->current_size, output_list);
    
    if (x->debug_enabled) {
        post("DEBUG: Final voice leading distance: %.2f", min_distance);
        post("DEBUG: Output chord: [%d %d %d %d]",
             (int)atom_getfloat(&output_list[0]),
             (int)atom_getfloat(&output_list[1]),
             (int)atom_getfloat(&output_list[2]),
             (int)atom_getfloat(&output_list[3]));
    }
}
// Class setup
void orbifold_setup(void) {
    // Initialize class with help symbol
    post("orbifold external version 1.0");
    orbifold_class = class_new(gensym("orbifold"),
        (t_newmethod)orbifold_new,
        (t_method)orbifold_free,
        sizeof(t_orbifold),
        CLASS_DEFAULT,
        A_DEFFLOAT, 0);
    
    // Register methods
    class_addlist(orbifold_class, orbifold_list);    // Hot inlet (main)
    class_addmethod(orbifold_class,                  // Cold inlet (chord)
        (t_method)orbifold_chord,
        gensym("chord"),
        A_GIMME, 0);
    class_addmethod(orbifold_class,                  // Cold inlet (root)
        (t_method)orbifold_root,
        gensym("root"),
        A_FLOAT, 0);
    class_addmethod(orbifold_class,
        (t_method)orbifold_debug,
        gensym("debug"),
        A_FLOAT, 0);
}
