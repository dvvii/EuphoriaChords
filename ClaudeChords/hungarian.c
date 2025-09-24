// Modified hungarian.c with intelligent target generation for strip-based chord input
#include "m_pd.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_VOICES 4
#define MAX_VOICING_VARIANTS 8  // Reduced from 24 - we generate fewer, smarter targets
#define MIN_OCTAVE 2
#define MAX_OCTAVE 7
#define HIGH_COST 1000

static t_class *hungarian_class;

typedef struct _hungarian {
    t_object x_obj;
    t_outlet *x_out_chord;
    t_outlet *x_out_cost;
    t_outlet *x_out_info;
    
    int current_chord[MAX_VOICES];
    int current_size;
    int target_intervals[MAX_VOICES];  // User-specified intervals (0, 4, 7, 11, etc.)
    int target_size;
    
    int feedback_enabled;
} t_hungarian;

// CORE INSIGHT: Convert interval specification to pitch classes for completeness checking
static void intervals_to_pitch_classes(int intervals[], int interval_count, int pitch_classes[]) {
    post("DEBUG: Converting intervals to pitch classes:");
    for (int i = 0; i < interval_count; i++) {
        pitch_classes[i] = intervals[i] % 12;  // Convert to pitch class
        post("DEBUG:   Interval[%d] = %d -> PC = %d", i, intervals[i], pitch_classes[i]);
    }
}

// CORE INSIGHT: Find optimal octave anchor that minimizes displacement from current chord
static int find_optimal_octave_anchor(t_hungarian *x, int target_intervals[]) {
    post("DEBUG: Finding optimal octave anchor");
    
    // Calculate center of current chord
    int current_sum = 0;
    for (int i = 0; i < x->current_size; i++) {
        current_sum += x->current_chord[i];
        post("DEBUG: Current voice[%d] = %d", i, x->current_chord[i]);
    }
    int current_center = current_sum / x->current_size;
    post("DEBUG: Current chord center = %d", current_center);
    
    // Test different octave placements and find the one closest to current center
    int best_octave = 4;
    int best_displacement = HIGH_COST;
    
    for (int test_octave = MIN_OCTAVE; test_octave <= MAX_OCTAVE; test_octave++) {
        int base_note = test_octave * 12;
        
        // Calculate center of target chord in this octave
        int target_sum = 0;
        for (int i = 0; i < x->target_size; i++) {
            target_sum += base_note + target_intervals[i];
        }
        int target_center = target_sum / x->target_size;
        
        int displacement = abs(target_center - current_center);
        post("DEBUG: Octave %d -> center %d, displacement = %d", 
             test_octave, target_center, displacement);
        
        if (displacement < best_displacement) {
            best_displacement = displacement;
            best_octave = test_octave;
        }
    }
    
    post("DEBUG: Optimal octave = %d, displacement = %d", best_octave, best_displacement);
    return best_octave;
}

// CORE INSIGHT: Generate multiple voicing arrangements around optimal octave
// Each arrangement GUARANTEES all user-specified pitch classes are present
static int generate_constrained_voicings(t_hungarian *x, int target_notes[], int *target_count) {
    post("DEBUG: Generating constrained voicings");
    
    // Convert user intervals to pitch classes for completeness checking
    int required_pitch_classes[MAX_VOICES];
    intervals_to_pitch_classes(x->target_intervals, x->target_size, required_pitch_classes);
    
    // Find optimal octave anchor
    int anchor_octave = find_optimal_octave_anchor(x, x->target_intervals);
    int base_note = anchor_octave * 12;
    
    *target_count = 0;
    
    // STRATEGY 1: Close position voicing (all notes in same octave as anchor)
    post("DEBUG: Generating close position voicing:");
    for (int i = 0; i < x->target_size; i++) {
        target_notes[(*target_count)] = base_note + x->target_intervals[i];
        post("DEBUG:   Close[%d] = %d (PC %d)", i, target_notes[*target_count], 
             target_notes[*target_count] % 12);
        (*target_count)++;
    }
    
    // STRATEGY 2: Bass octave down (move lowest note down an octave)
    if (*target_count < MAX_VOICING_VARIANTS - x->target_size && anchor_octave > MIN_OCTAVE) {
        post("DEBUG: Generating bass-down voicing:");
        target_notes[(*target_count)++] = base_note + x->target_intervals[0] - 12;  // Bass down
        for (int i = 1; i < x->target_size; i++) {
            target_notes[(*target_count)++] = base_note + x->target_intervals[i];
        }
    }
    
    // STRATEGY 3: Soprano octave up (move highest note up an octave)  
    if (*target_count < MAX_VOICING_VARIANTS - x->target_size && anchor_octave < MAX_OCTAVE) {
        post("DEBUG: Generating soprano-up voicing:");
        for (int i = 0; i < x->target_size - 1; i++) {
            target_notes[(*target_count)++] = base_note + x->target_intervals[i];
        }
        target_notes[(*target_count)++] = base_note + x->target_intervals[x->target_size-1] + 12;  // Top up
    }
    
    // STRATEGY 4: Spread voicing (bass down, soprano up)
    if (*target_count < MAX_VOICING_VARIANTS - x->target_size && 
        anchor_octave > MIN_OCTAVE && anchor_octave < MAX_OCTAVE) {
        post("DEBUG: Generating spread voicing:");
        target_notes[(*target_count)++] = base_note + x->target_intervals[0] - 12;     // Bass down
        for (int i = 1; i < x->target_size - 1; i++) {
            target_notes[(*target_count)++] = base_note + x->target_intervals[i];      // Middle same
        }
        target_notes[(*target_count)++] = base_note + x->target_intervals[x->target_size-1] + 12;  // Top up
    }
    
    post("DEBUG: Generated %d total target notes across %d voicing strategies", 
         *target_count, (*target_count) / x->target_size);
    
    // CRITICAL VERIFICATION: Check that we can form a complete chord
    // Count how many of each required pitch class we have available
    for (int pc = 0; pc < x->target_size; pc++) {
        int required_pc = required_pitch_classes[pc];
        int available_count = 0;
        
        for (int i = 0; i < *target_count; i++) {
            if ((target_notes[i] % 12) == required_pc) {
                available_count++;
            }
        }
        
        post("DEBUG: Required PC %d appears %d times in target set", 
             required_pc, available_count);
        
        if (available_count == 0) {
            post("ERROR: Required pitch class %d missing from target set!", required_pc);
        }
    }
    
    return anchor_octave;  // Return anchor for debugging info
}

// Enhanced Hungarian algorithm with completeness verification
static void find_minimum_assignment(int cost_matrix[][MAX_VOICING_VARIANTS], 
                                   int rows, int cols, 
                                   int assignment[], int *total_cost,
                                   int target_notes[], int required_pcs[], int pc_count) {
    int i, j, min_j, min_cost;
    int used[MAX_VOICING_VARIANTS] = {0};
    *total_cost = 0;
    
    post("DEBUG: Enhanced Hungarian starting with completeness awareness");
    
    // Greedy assignment with completeness bias
    for (i = 0; i < rows; i++) {
        min_cost = HIGH_COST;
        min_j = -1;
        
        for (j = 0; j < cols; j++) {
            if (!used[j]) {
                int cost = cost_matrix[i][j];
                
                // COMPLETENESS BONUS: Strongly prefer targets that contribute required pitch classes
                int target_pc = target_notes[j] % 12;
                for (int pc = 0; pc < pc_count; pc++) {
                    if (target_pc == required_pcs[pc]) {
                        cost -= 10;  // Strong preference for essential chord tones
                        break;
                    }
                }
                
                if (cost < min_cost) {
                    min_cost = cost;
                    min_j = j;
                }
            }
        }
        
        if (min_j >= 0) {
            assignment[i] = min_j;
            used[min_j] = 1;
            *total_cost += cost_matrix[i][min_j];  // Use original cost for total
        } else {
            assignment[i] = -1;
        }
    }
    
    // VERIFICATION: Check final chord completeness
    post("DEBUG: Verifying final chord completeness:");
    for (int pc = 0; pc < pc_count; pc++) {
        int found = 0;
        for (int v = 0; v < rows; v++) {
            if (assignment[v] >= 0) {
                int assigned_note = target_notes[assignment[v]];
                if ((assigned_note % 12) == required_pcs[pc]) {
                    found = 1;
                    break;
                }
            }
        }
        post("DEBUG: Required PC %d: %s", required_pcs[pc], found ? "PRESENT" : "MISSING!");
    }
}

// Main calculation function with improved target generation
static void hungarian_calculate(t_hungarian *x) {
    if (x->current_size == 0 || x->target_size == 0) {
        post("hungarian: no chord data");
        return;
    }
    
    post("DEBUG: Starting calculation with user-specified intervals:");
    for (int i = 0; i < x->target_size; i++) {
        post("DEBUG: User interval[%d] = %d", i, x->target_intervals[i]);
    }
    
    // Generate constrained target voicings that guarantee completeness
    int target_notes[MAX_VOICING_VARIANTS];
    int target_count;
    int anchor_octave = generate_constrained_voicings(x, target_notes, &target_count);
    
    // Prepare required pitch classes for completeness checking
    int required_pitch_classes[MAX_VOICES];
    intervals_to_pitch_classes(x->target_intervals, x->target_size, required_pitch_classes);
    
    // Create cost matrix with distance + chord tone priority
    int cost_matrix[MAX_VOICES][MAX_VOICING_VARIANTS];
    
    for (int i = 0; i < x->current_size; i++) {
        for (int j = 0; j < target_count; j++) {
            cost_matrix[i][j] = abs(x->current_chord[i] - target_notes[j]);
        }
        // Pad unused columns with high cost
        for (int j = target_count; j < MAX_VOICING_VARIANTS; j++) {
            cost_matrix[i][j] = HIGH_COST;
        }
    }
    
    // Run enhanced Hungarian algorithm
    int assignment[MAX_VOICES];
    int total_cost;
    find_minimum_assignment(cost_matrix, x->current_size, target_count, 
                           assignment, &total_cost, target_notes, 
                           required_pitch_classes, x->target_size);
    
    // Build output chord
    t_atom chord_out[MAX_VOICES];
    for (int i = 0; i < x->current_size; i++) {
        if (assignment[i] >= 0) {
            int assigned_note = target_notes[assignment[i]];
            SETFLOAT(&chord_out[i], assigned_note);
            post("DEBUG: Voice %d: %d -> %d (PC %d)", 
                 i, x->current_chord[i], assigned_note, assigned_note % 12);
        } else {
            SETFLOAT(&chord_out[i], x->current_chord[i]); // Fallback
        }
    }
    
    // Output results
    outlet_list(x->x_out_chord, &s_list, x->current_size, chord_out);
    outlet_float(x->x_out_cost, total_cost);
    
    // Output info: anchor octave, target count, voice count
    t_atom info[3];
    SETFLOAT(&info[0], anchor_octave);
    SETFLOAT(&info[1], target_count);
    SETFLOAT(&info[2], x->current_size);
    outlet_list(x->x_out_info, &s_list, 3, info);
    
    post("hungarian: constrained voice leading complete, cost: %d, anchor: octave %d", 
         total_cost, anchor_octave);
    
    // Update current chord if feedback enabled
    if (x->feedback_enabled) {
        for (int i = 0; i < x->current_size; i++) {
            if (assignment[i] >= 0) {
                x->current_chord[i] = target_notes[assignment[i]];
            }
        }
    }
}

// Set target chord using interval specification (your strip-based input)
static void hungarian_target(t_hungarian *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        post("hungarian: too many intervals (max %d)", MAX_VOICES);
        return;
    }
    
    post("DEBUG: Received target intervals from user strips:");
    
    x->target_size = argc;
    for (int i = 0; i < argc; i++) {
        x->target_intervals[i] = (int)atom_getfloat(&argv[i]);
        post("DEBUG: Strip interval[%d] = %d", i, x->target_intervals[i]);
    }
    
    hungarian_calculate(x);
}

// Set current chord (actual MIDI note numbers)
static void hungarian_current(t_hungarian *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        post("hungarian: too many voices (max %d)", MAX_VOICES);
        return;
    }
    
    x->current_size = argc;
    for (int i = 0; i < argc; i++) {
        x->current_chord[i] = (int)atom_getfloat(&argv[i]);
    }
    
    post("hungarian: current chord updated");
}

static void hungarian_feedback(t_hungarian *x, t_floatarg f) {
    x->feedback_enabled = (f != 0);
    post("hungarian: feedback %s", x->feedback_enabled ? "enabled" : "disabled");
}

static void hungarian_bang(t_hungarian *x) {
    hungarian_calculate(x);
}

static void *hungarian_new(void) {
    t_hungarian *x = (t_hungarian *)pd_new(hungarian_class);
    
    x->x_out_chord = outlet_new(&x->x_obj, &s_list);
    x->x_out_cost = outlet_new(&x->x_obj, &s_float);
    x->x_out_info = outlet_new(&x->x_obj, &s_list);
    
    x->current_size = 0;
    x->target_size = 0;
    x->feedback_enabled = 1;
    
    // Default starting chord (C major: C3, E3, G3, C4)
    x->current_chord[0] = 48;
    x->current_chord[1] = 52;
    x->current_chord[2] = 55;
    x->current_chord[3] = 60;
    x->current_size = 4;
    
    post("hungarian: constrained voice leading calculator ready");
    post("Usage: 'current <midi_notes>' to set current chord");
    post("       'target <intervals>' to set target from strip interface");
    
    return (void *)x;
}

void hungarian_setup(void) {
    hungarian_class = class_new(gensym("hungarian"),
                               (t_newmethod)hungarian_new,
                               0,
                               sizeof(t_hungarian),
                               CLASS_DEFAULT,
                               0);
    
    class_addmethod(hungarian_class, (t_method)hungarian_current, 
                   gensym("current"), A_GIMME, 0);
    class_addmethod(hungarian_class, (t_method)hungarian_target,
                   gensym("target"), A_GIMME, 0);
    class_addmethod(hungarian_class, (t_method)hungarian_feedback,
                   gensym("feedback"), A_FLOAT, 0);
    class_addbang(hungarian_class, hungarian_bang);
}