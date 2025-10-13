// Message routing:
//   'current <notes>' - Set current chord (COLD)
//   'root <0-11>'     - Set root interval (COLD)
//   'chord <ints>'    - Set target chord intervals (HOT - triggers calculation!)
//
// Outlets: [bass] [chord] [cost] [info]

#include "m_pd.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_VOICES 8
#define STABLE_CENTROID 60.0f  // C4 - fixed register target

static t_class *orbifold_class;

typedef struct _orbifold {
    t_object x_obj;
    t_outlet *x_out_bass;
    t_outlet *x_out_chord;
    t_outlet *x_out_cost;
    t_outlet *x_out_info;
    
    int current_chord[MAX_VOICES];
    int current_size;
    int root_interval;
    int chord_intervals[MAX_VOICES];
    int chord_size;
    
    int feedback_enabled;
    int debug_enabled;
} t_orbifold;

// Reduce chord to prime form (sorted PCs starting at 0)
static void reduce_to_prime_form(int *chord, int size, int *prime) {
    // Step 1: Convert to pitch classes
    int pcs[MAX_VOICES];
    for (int i = 0; i < size; i++) {
        pcs[i] = chord[i] % 12;
        if (pcs[i] < 0) pcs[i] += 12;
    }
    
    // Step 2: Sort
    for (int i = 0; i < size - 1; i++) {
        for (int j = i + 1; j < size; j++) {
            if (pcs[j] < pcs[i]) {
                int temp = pcs[i];
                pcs[i] = pcs[j];
                pcs[j] = temp;
            }
        }
    }
    
    // Step 3: Normalize to start at 0
    int base = pcs[0];
    for (int i = 0; i < size; i++) {
        prime[i] = (pcs[i] - base + 12) % 12;
    }
}

static int place_around_centroid(int pitch_class, float centroid) {
    int best_note = 60;  // default to middle C
    float min_distance = 10000.0f;
    int base_octave = (int)(centroid / 12.0f);
    
    // Test octaves around stable centroid
    for (int oct_offset = -2; oct_offset <= 2; oct_offset++) {
        int octave = base_octave + oct_offset;
        if (octave < 2 || octave > 6) continue;
        
        int candidate = octave * 12 + pitch_class;
        float distance = fabs(candidate - centroid);
        
        if (distance < min_distance) {
            min_distance = distance;
            best_note = candidate;
        }
    }
    
    return best_note;
}

// Calculate optimal mapping considering voice leading distances
static float calculate_voice_leading(int *current, int current_size,
                                     int *target, int target_size,
                                     int *mapping) {
    float total_distance = 0.0f;
    int used[MAX_VOICES] = {0};
    
    // Greedy assignment: each current note finds closest unused target
    for (int i = 0; i < current_size; i++) {
        int best_j = -1;
        float min_dist = 10000.0f;
        
        for (int j = 0; j < target_size; j++) {
            if (used[j]) continue;
            
            // Calculate semitone distance
            float dist = fabs(current[i] - target[j]);
            
            if (dist < min_dist) {
                min_dist = dist;
                best_j = j;
            }
        }
        
        if (best_j >= 0) {
            mapping[i] = best_j;
            used[best_j] = 1;
            total_distance += min_dist;
        } else {
            mapping[i] = -1;
        }
    }
    
    return total_distance;
}

// Main calculation
static void orbifold_calculate(t_orbifold *x) {
    if (x->current_size == 0 || x->chord_size == 0) {
        post("orbifold: missing chord data");
        return;
    }
    
    if (x->debug_enabled) {
        post("\n=== ORBIFOLD (STABLE CENTROID) ===");
        post("Current: [%d %d %d %d]", 
             x->current_chord[0], x->current_chord[1],
             x->current_chord[2], x->current_chord[3]);
        post("Root: %d, Intervals: [%d %d %d %d]",
             x->root_interval, x->chord_intervals[0], x->chord_intervals[1],
             x->chord_intervals[2], x->chord_intervals[3]);
    }
    
    // STEP 1: Reduce current chord to prime form (for analysis)
    int current_prime[MAX_VOICES];
    reduce_to_prime_form(x->current_chord, x->current_size, current_prime);
    
    if (x->debug_enabled) {
        post("Current prime form: [%d %d %d %d]",
             current_prime[0], current_prime[1],
             current_prime[2], current_prime[3]);
    }
    
    // STEP 2: Build target pitch classes
    int target_pc[MAX_VOICES];
    for (int i = 0; i < x->chord_size; i++) {
        target_pc[i] = (x->root_interval + x->chord_intervals[i]) % 12;
        if (target_pc[i] < 0) target_pc[i] += 12;
    }
    
    if (x->debug_enabled) {
        post("Target PCs: [%d %d %d %d]",
             target_pc[0], target_pc[1],
             target_pc[2], target_pc[3]);
    }
    
    // STEP 3: Place target PCs around STABLE centroid (not calculated from current!)
    int target_voicing[MAX_VOICES];
    for (int i = 0; i < x->chord_size; i++) {
        target_voicing[i] = place_around_centroid(target_pc[i], STABLE_CENTROID);
    }
    
    // Sort for canonical ordering
    for (int i = 0; i < x->chord_size - 1; i++) {
        for (int j = i + 1; j < x->chord_size; j++) {
            if (target_voicing[j] < target_voicing[i]) {
                int temp = target_voicing[i];
                target_voicing[i] = target_voicing[j];
                target_voicing[j] = temp;
            }
        }
    }
    
    if (x->debug_enabled) {
        post("Target voicing (around C4=60): [%d %d %d %d]",
             target_voicing[0], target_voicing[1],
             target_voicing[2], target_voicing[3]);
    }
    
    // STEP 4: Calculate voice mapping
    int mapping[MAX_VOICES];
    float voice_leading_distance = calculate_voice_leading(
        x->current_chord, x->current_size,
        target_voicing, x->chord_size,
        mapping
    );
    
    if (x->debug_enabled) {
        post("Voice leading distance: %.2f semitones", voice_leading_distance);
    }
    
    // STEP 5: Create output chord
    t_atom output_chord[MAX_VOICES];
    int output[MAX_VOICES];
    
    for (int i = 0; i < x->current_size; i++) {
        if (mapping[i] >= 0) {
            output[i] = target_voicing[mapping[i]];
            SETFLOAT(&output_chord[i], output[i]);
        }
    }
    
    // STEP 6: Calculate bass (one octave below lowest voice)
    int lowest_voice = output[0];
    for (int i = 1; i < x->current_size; i++) {
        if (output[i] < lowest_voice) {
            lowest_voice = output[i];
        }
    }
    
    int bass_octave = (lowest_voice / 12) - 1;
    if (bass_octave < 2) bass_octave = 2;
    int bass_note = bass_octave * 12 + x->root_interval;
    
    if (x->debug_enabled) {
        post("Bass: %d, Output: [%d %d %d %d]",
             bass_note, output[0], output[1], output[2], output[3]);
        
        // Verify against stable centroid
        float actual_centroid = 0;
        for (int i = 0; i < x->current_size; i++) {
            actual_centroid += output[i];
        }
        actual_centroid /= x->current_size;
        post("Output centroid: %.2f (target was %.2f)", 
             actual_centroid, STABLE_CENTROID);
        post("=== COMPLETE ===\n");
    }
    
    // Output (rightmost first)
    t_atom info[3];
    SETFLOAT(&info[0], STABLE_CENTROID);
    SETFLOAT(&info[1], voice_leading_distance);
    SETFLOAT(&info[2], x->current_size);
    outlet_list(x->x_out_info, &s_list, 3, info);
    
    outlet_float(x->x_out_cost, voice_leading_distance);
    outlet_list(x->x_out_chord, &s_list, x->current_size, output_chord);
    outlet_float(x->x_out_bass, bass_note);
    
    // Update feedback
    if (x->feedback_enabled) {
        for (int i = 0; i < x->current_size; i++) {
            if (mapping[i] >= 0) {
                x->current_chord[i] = output[i];
            }
        }
    }
}

// Set current chord (COLD)
static void orbifold_current(t_orbifold *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        pd_error(x, "orbifold: too many voices (max %d)", MAX_VOICES);
        return;
    }
    
    x->current_size = argc;
    for (int i = 0; i < argc; i++) {
        x->current_chord[i] = (int)atom_getfloat(&argv[i]);
    }
    
    if (x->debug_enabled) {
        post("orbifold: current set to [%d %d %d %d]",
             x->current_chord[0], x->current_chord[1],
             x->current_chord[2], x->current_chord[3]);
    }
}

// Set root interval (COLD)
static void orbifold_root(t_orbifold *x, t_floatarg f) {
    int root = (int)f % 12;
    if (root < 0) root += 12;
    x->root_interval = root;
    
    if (x->debug_enabled) {
        post("orbifold: root set to %d", x->root_interval);
    }
}

// Set chord intervals (HOT)
static void orbifold_chord(t_orbifold *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        pd_error(x, "orbifold: too many intervals (max %d)", MAX_VOICES);
        return;
    }
    
    x->chord_size = argc;
    for (int i = 0; i < argc; i++) {
        x->chord_intervals[i] = (int)atom_getfloat(&argv[i]);
    }
    
    if (x->debug_enabled) {
        post("orbifold: chord set to [%d %d %d %d]",
             x->chord_intervals[0], x->chord_intervals[1],
             x->chord_intervals[2], x->chord_intervals[3]);
    }
    
    if (x->current_size > 0) {
        orbifold_calculate(x);
    } else {
        pd_error(x, "orbifold: no current chord set");
    }
}

// Toggle feedback
static void orbifold_feedback(t_orbifold *x, t_floatarg f) {
    x->feedback_enabled = (f != 0);
    post("orbifold: feedback %s", x->feedback_enabled ? "enabled" : "disabled");
}

// Toggle debug
static void orbifold_debug(t_orbifold *x, t_floatarg f) {
    x->debug_enabled = (f != 0);
    post("orbifold: debug %s", x->debug_enabled ? "enabled" : "disabled");
}

// Bang
static void orbifold_bang(t_orbifold *x) {
    orbifold_calculate(x);
}

// Constructor
static void *orbifold_new(void) {
    t_orbifold *x = (t_orbifold *)pd_new(orbifold_class);
    
    x->x_out_info = outlet_new(&x->x_obj, &s_list);
    x->x_out_cost = outlet_new(&x->x_obj, &s_float);
    x->x_out_chord = outlet_new(&x->x_obj, &s_list);
    x->x_out_bass = outlet_new(&x->x_obj, &s_float);
    
    x->current_size = 0;
    x->chord_size = 0;
    x->root_interval = 0;
    x->feedback_enabled = 1;
    x->debug_enabled = 0;
    
    // Default C major
    x->current_chord[0] = 48;
    x->current_chord[1] = 52;
    x->current_chord[2] = 55;
    x->current_chord[3] = 60;
    x->current_size = 4;
    
    post("orbifold: stable centroid voice leading (C4=60)");
    post("Outlets: [bass] [chord] [cost] [info]");
    
    return (void *)x;
}

void orbifold_setup(void) {
    orbifold_class = class_new(gensym("orbifold"),
                               (t_newmethod)orbifold_new,
                               0,
                               sizeof(t_orbifold),
                               CLASS_DEFAULT,
                               0);
    
    class_addmethod(orbifold_class, (t_method)orbifold_current,
                   gensym("current"), A_GIMME, 0);
    class_addmethod(orbifold_class, (t_method)orbifold_root,
                   gensym("root"), A_FLOAT, 0);
    class_addmethod(orbifold_class, (t_method)orbifold_chord,
                   gensym("chord"), A_GIMME, 0);
    class_addmethod(orbifold_class, (t_method)orbifold_feedback,
                   gensym("feedback"), A_FLOAT, 0);
    class_addmethod(orbifold_class, (t_method)orbifold_debug,
                   gensym("debug"), A_FLOAT, 0);
    class_addbang(orbifold_class, orbifold_bang);
}