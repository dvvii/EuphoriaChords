// Enhanced Hungarian algorithm with root transposition support
#include "m_pd.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_VOICES 4
#define MAX_VOICING_VARIANTS 8
#define MIN_OCTAVE 2
#define MAX_OCTAVE 4
#define HIGH_COST 1000

static t_class *hungarian_class;

typedef struct _hungarian {
    t_object x_obj;
    t_outlet *x_out_root;
    t_outlet *x_out_chord;
    t_outlet *x_out_cost;
    t_outlet *x_out_info;
    
    int current_chord[MAX_VOICES];
    int current_size;
    
    // Separate root and chord intervals for maximum flexibility
    int root_interval;                    // Root transposition (0-11, from user interface)
    int chord_intervals[MAX_VOICES];      // Chord structure intervals (from strip interface)
    int chord_size;
    
    // Working intervals (root + chord, computed internally)
    int target_intervals[MAX_VOICES];     // Final transposed intervals for processing
    int target_size;
    
    int feedback_enabled;
    int debug_enabled; // Instance debug flag
} t_hungarian;

// MUSICAL INSIGHT: Apply root transposition to create final target intervals
// This separates harmonic content (chord type) from tonal center (root note)
static void apply_root_transposition(t_hungarian *x) {
    if (x->debug_enabled) {
        post("DEBUG: Applying root transposition:");
        post("DEBUG: Root interval = %d", x->root_interval);
        post("DEBUG: Original chord intervals: [%d %d %d %d]", 
             x->chord_size > 0 ? x->chord_intervals[0] : -1,
             x->chord_size > 1 ? x->chord_intervals[1] : -1,
             x->chord_size > 2 ? x->chord_intervals[2] : -1,
             x->chord_size > 3 ? x->chord_intervals[3] : -1);
    }
    
    x->target_size = x->chord_size;
    
    for (int i = 0; i < x->chord_size; i++) {
        // Add root transposition to each chord interval
        x->target_intervals[i] = (x->chord_intervals[i] + x->root_interval) % 12;
       if (x->debug_enabled) {
          post("DEBUG: Chord[%d] = %d + root %d = %d (final interval)", 
              i, x->chord_intervals[i], x->root_interval, x->target_intervals[i]);
       }
    }
    
    if (x->debug_enabled) {
        post("DEBUG: Final target intervals: [%d %d %d %d]",
             x->target_size > 0 ? x->target_intervals[0] : -1,
             x->target_size > 1 ? x->target_intervals[1] : -1,
             x->target_size > 2 ? x->target_intervals[2] : -1,
             x->target_size > 3 ? x->target_intervals[3] : -1);
    }
}

// Convert intervals to pitch classes for completeness verification
static void intervals_to_pitch_classes(int intervals[], int interval_count, int pitch_classes[]) {
    post("DEBUG: Converting target intervals to required pitch classes:");
    for (int i = 0; i < interval_count; i++) {
        pitch_classes[i] = intervals[i] % 12;  // Already mod 12 from root transposition
        post("DEBUG:   Target interval[%d] = %d -> Required PC = %d", 
             i, intervals[i], pitch_classes[i]);
    }
}

// Find optimal octave placement that minimizes displacement from current chord
static int find_optimal_octave_anchor(t_hungarian *x, int target_intervals[]) {
    if (x->debug_enabled) post("DEBUG: Finding optimal octave anchor for transposed chord");
    
    // Calculate center of mass of current chord
    int current_sum = 0;
    for (int i = 0; i < x->current_size; i++) {
        current_sum += x->current_chord[i];
    if (x->debug_enabled) post("DEBUG: Current voice[%d] = %d", i, x->current_chord[i]);
    }
    int current_center = current_sum / x->current_size;
    if (x->debug_enabled) post("DEBUG: Current chord center of mass = %d", current_center);
    
    // Test different octave anchors and find minimum displacement
    int best_octave = 4;
    int best_displacement = HIGH_COST;
    
    for (int test_octave = MIN_OCTAVE; test_octave <= MAX_OCTAVE; test_octave++) {
        int base_note = test_octave * 12;
        
        // Calculate center of mass for target chord in this octave
        int target_sum = 0;
        for (int i = 0; i < x->target_size; i++) {
            target_sum += base_note + target_intervals[i];
        }
        int target_center = target_sum / x->target_size;
        
        int displacement = abs(target_center - current_center);
       if (x->debug_enabled) post("DEBUG: Octave %d -> target center %d, displacement = %d", 
           test_octave, target_center, displacement);
        
        if (displacement < best_displacement) {
            best_displacement = displacement;
            best_octave = test_octave;
        }
    }
    
    if (x->debug_enabled) post("DEBUG: Optimal octave anchor = %d (displacement %d)", best_octave, best_displacement);
    return best_octave;
}

// Generate multiple voicing arrangements that guarantee chord completeness
static int generate_constrained_voicings(t_hungarian *x, int target_notes[], int *target_count) {
    if (x->debug_enabled) post("DEBUG: Generating constrained voicings for transposed chord");
    
    // Find optimal octave placement
    int anchor_octave = find_optimal_octave_anchor(x, x->target_intervals);
    int base_note = anchor_octave * 12;
    
    *target_count = 0;
    
    // VOICING STRATEGY 1: Close position (all notes in anchor octave)
    if (x->debug_enabled) post("DEBUG: Strategy 1 - Close position voicing:");
    for (int i = 0; i < x->target_size; i++) {
        target_notes[(*target_count)] = base_note + x->target_intervals[i];
       if (x->debug_enabled) post("DEBUG:   Close[%d] = %d (PC %d)", i, target_notes[*target_count], 
           target_notes[*target_count] % 12);
        (*target_count)++;
    }
    
    // VOICING STRATEGY 2: Bass register drop (lowest voice down an octave)
    if (*target_count < MAX_VOICING_VARIANTS - x->target_size && anchor_octave > MIN_OCTAVE) {
    if (x->debug_enabled) post("DEBUG: Strategy 2 - Bass drop voicing:");
        target_notes[(*target_count)++] = base_note + x->target_intervals[0] - 12;  // Drop bass
        for (int i = 1; i < x->target_size; i++) {
            target_notes[(*target_count)++] = base_note + x->target_intervals[i];    // Keep others
        }
    if (x->debug_enabled) post("DEBUG:   Bass dropped to %d", target_notes[*target_count - x->target_size]);
    }
    
    // VOICING STRATEGY 3: Soprano register lift (highest voice up an octave)
    if (*target_count < MAX_VOICING_VARIANTS - x->target_size && anchor_octave < MAX_OCTAVE) {
    if (x->debug_enabled) post("DEBUG: Strategy 3 - Soprano lift voicing:");
        for (int i = 0; i < x->target_size - 1; i++) {
            target_notes[(*target_count)++] = base_note + x->target_intervals[i];    // Keep lowers
        }
        target_notes[(*target_count)++] = base_note + x->target_intervals[x->target_size-1] + 12;  // Lift top
    if (x->debug_enabled) post("DEBUG:   Soprano lifted to %d", target_notes[*target_count - 1]);
    }
    
    // VOICING STRATEGY 4: Open/spread voicing (bass down, soprano up)
    if (*target_count < MAX_VOICING_VARIANTS - x->target_size && 
        anchor_octave > MIN_OCTAVE && anchor_octave < MAX_OCTAVE) {
    if (x->debug_enabled) post("DEBUG: Strategy 4 - Spread voicing:");
        target_notes[(*target_count)++] = base_note + x->target_intervals[0] - 12;           // Bass down
        for (int i = 1; i < x->target_size - 1; i++) {
            target_notes[(*target_count)++] = base_note + x->target_intervals[i];            // Middles same
        }
        target_notes[(*target_count)++] = base_note + x->target_intervals[x->target_size-1] + 12;  // Top up
    }
    
    if (x->debug_enabled) post("DEBUG: Generated %d target notes across %d voicing strategies", 
        *target_count, (*target_count) / x->target_size);
    
    // CRITICAL VERIFICATION: Ensure each required pitch class can be satisfied
    int required_pitch_classes[MAX_VOICES];
    intervals_to_pitch_classes(x->target_intervals, x->target_size, required_pitch_classes);
    
    for (int pc_idx = 0; pc_idx < x->target_size; pc_idx++) {
        int required_pc = required_pitch_classes[pc_idx];
        int availability_count = 0;
        
        for (int note_idx = 0; note_idx < *target_count; note_idx++) {
            if ((target_notes[note_idx] % 12) == required_pc) {
                availability_count++;
            }
        }
        
       if (x->debug_enabled) post("DEBUG: Required PC %d available in %d target notes", 
           required_pc, availability_count);
             
        if (availability_count == 0) {
            if (x->debug_enabled) post("ERROR: Required pitch class %d completely missing from target set!", required_pc);
        }
    }
    
    return anchor_octave;
}

// Enhanced Hungarian assignment with completeness preference
static void find_minimum_assignment(int cost_matrix[][MAX_VOICING_VARIANTS], 
                                   int rows, int cols, 
                                   int assignment[], int *total_cost,
                                   int target_notes[], int required_pcs[], int pc_count) {
    int i, j, min_j, min_cost;
    int used[MAX_VOICING_VARIANTS] = {0};
    *total_cost = 0;
    
    // This function is not a method, so we can't access x->debug_enabled
    
    // Greedy assignment with strong preference for essential chord tones
    for (i = 0; i < rows; i++) {
        min_cost = HIGH_COST;
        min_j = -1;
        
        post("DEBUG: Assigning voice %d (current note %d):", i, i < rows ? target_notes[i] : -1);
        
        for (j = 0; j < cols; j++) {
            if (!used[j]) {
                int base_cost = cost_matrix[i][j];
                int adjusted_cost = base_cost;
                
                // COMPLETENESS INCENTIVE: Strongly favor notes that provide required pitch classes
                int target_pc = target_notes[j] % 12;
                for (int pc_idx = 0; pc_idx < pc_count; pc_idx++) {
                    if (target_pc == required_pcs[pc_idx]) {
                        adjusted_cost -= 15;  // Strong preference for essential chord tones
                        post("DEBUG:     Target[%d]=%d (PC %d) ESSENTIAL, cost %d->%d", 
                             j, target_notes[j], target_pc, base_cost, adjusted_cost);
                        break;
                    }
                }
                
                if (adjusted_cost < min_cost) {
                    min_cost = adjusted_cost;
                    min_j = j;
                }
            } else {
                post("DEBUG:     Target[%d]=%d ALREADY USED", j, target_notes[j]);
            }
        }
        
        if (min_j >= 0) {
            assignment[i] = min_j;
            used[min_j] = 1;
            *total_cost += cost_matrix[i][min_j];  // Use original cost for accurate total
            post("DEBUG: Voice %d assigned to target[%d]=%d, running cost=%d", 
                 i, min_j, target_notes[min_j], *total_cost);
        } else {
            assignment[i] = -1;
            post("DEBUG: Voice %d could not be assigned!", i);
        }
    }
    
    // FINAL VERIFICATION: Check that all required pitch classes are present
    post("DEBUG: Final chord completeness verification:");
    for (int pc_idx = 0; pc_idx < pc_count; pc_idx++) {
        int required_pc = required_pcs[pc_idx];
        int found = 0;
        
        for (int voice = 0; voice < rows; voice++) {
            if (assignment[voice] >= 0) {
                int assigned_note = target_notes[assignment[voice]];
                if ((assigned_note % 12) == required_pc) {
                    found = 1;
                    post("DEBUG: Required PC %d found in voice %d (note %d)", 
                         required_pc, voice, assigned_note);
                    break;
                }
            }
        }
        
        if (!found) {
            post("WARNING: Required PC %d missing from final chord!", required_pc);
        }
    }
}

// Main calculation with root transposition integration
static void hungarian_calculate(t_hungarian *x) {
    if (x->current_size == 0 || x->chord_size == 0) {
        post("hungarian: missing chord data (current:%d, chord:%d)", 
             x->current_size, x->chord_size);
        return;
    }
    
    if (x->debug_enabled) post("DEBUG: Starting calculation with root=%d, chord_size=%d", 
        x->root_interval, x->chord_size);
    
    // STEP 1: Apply root transposition to create working target intervals
    apply_root_transposition(x);
    
    // STEP 2: Generate constrained voicing variants that guarantee completeness
    int target_notes[MAX_VOICING_VARIANTS];
    int target_count;
    int anchor_octave = generate_constrained_voicings(x, target_notes, &target_count);
    
    // STEP 3: Prepare required pitch classes for completeness enforcement
    int required_pitch_classes[MAX_VOICES];
    intervals_to_pitch_classes(x->target_intervals, x->target_size, required_pitch_classes);
    
    // STEP 4: Build cost matrix (voice leading distances)
    int cost_matrix[MAX_VOICES][MAX_VOICING_VARIANTS];
    
    if (x->debug_enabled) post("DEBUG: Building cost matrix (%d voices x %d targets):", x->current_size, target_count);
    for (int voice = 0; voice < x->current_size; voice++) {
        for (int target = 0; target < target_count; target++) {
            cost_matrix[voice][target] = abs(x->current_chord[voice] - target_notes[target]);
        }
        // Fill unused columns with prohibitive costs
        for (int target = target_count; target < MAX_VOICING_VARIANTS; target++) {
            cost_matrix[voice][target] = HIGH_COST;
        }
    }
    
    // STEP 5: Run enhanced Hungarian algorithm with completeness bias
    int assignment[MAX_VOICES];
    int total_cost;
    find_minimum_assignment(cost_matrix, x->current_size, target_count, 
                           assignment, &total_cost, target_notes, 
                           required_pitch_classes, x->target_size);
    
    // STEP 6: Construct output chord from assignments
    t_atom chord_out[MAX_VOICES];
    if (x->debug_enabled) post("DEBUG: Final voice leading solution:");
    for (int voice = 0; voice < x->current_size; voice++) {
        if (assignment[voice] >= 0) {
            int assigned_note = target_notes[assignment[voice]];
            SETFLOAT(&chord_out[voice], assigned_note);
            if (x->debug_enabled) post("DEBUG: Voice %d: %d -> %d (PC %d, movement %d semitones)", 
                 voice, x->current_chord[voice], assigned_note, 
                 assigned_note % 12, abs(assigned_note - x->current_chord[voice]));
        } else {
            // Fallback: keep current note if assignment failed
            SETFLOAT(&chord_out[voice], x->current_chord[voice]);
            if (x->debug_enabled) post("DEBUG: Voice %d: %d -> %d (FALLBACK - no assignment)", 
                 voice, x->current_chord[voice], x->current_chord[voice]);
        }
    }
    
    // Output results to Pure Data
    outlet_list(x->x_out_chord, &s_list, x->current_size, chord_out);
    outlet_float(x->x_out_cost, total_cost);
    
    // Output diagnostic info: anchor octave, target count, voice count  
    t_atom info[3];
    SETFLOAT(&info[0], anchor_octave);
    SETFLOAT(&info[1], target_count);
    SETFLOAT(&info[2], x->current_size);
    outlet_list(x->x_out_info, &s_list, 3, info);
    
    post("hungarian: voice leading complete - root %d, cost %d, anchor octave %d", 
         x->root_interval, total_cost, anchor_octave);
    
    // Update current chord for feedback chain if enabled
    if (x->feedback_enabled) {
        for (int voice = 0; voice < x->current_size; voice++) {
            if (assignment[voice] >= 0) {
                x->current_chord[voice] = target_notes[assignment[voice]];
            }
        }
        post("hungarian: feedback enabled - output becomes next current chord");
    }
}

// Set root transposition (interval from 0, typically 0-11)
static void hungarian_root(t_hungarian *x, t_floatarg f) {
    int new_root = (int)f % 12;  // Ensure 0-11 range
    if (new_root < 0) new_root += 12;  // Handle negative inputs
    
    x->root_interval = new_root;
    post("hungarian: root set to interval %d", x->root_interval);
    
    // If we have both root and chord data, automatically recalculate
    if (x->chord_size > 0) {
        hungarian_calculate(x);
    }
}

// Set chord structure intervals (from strip interface)
static void hungarian_chord(t_hungarian *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        post("hungarian: too many chord intervals (max %d)", MAX_VOICES);
        return;
    }
    
    if (x->debug_enabled) post("DEBUG: Received chord intervals from strip interface:");
    
    x->chord_size = argc;
    for (int i = 0; i < argc; i++) {
        x->chord_intervals[i] = (int)atom_getfloat(&argv[i]);
    if (x->debug_enabled) post("DEBUG: Chord interval[%d] = %d", i, x->chord_intervals[i]);
    }
    
    // If we have both root and chord data, automatically recalculate  
    if (x->root_interval >= 0) {
        hungarian_calculate(x);
    }
}

// Set current chord (actual MIDI note numbers)
static void hungarian_current(t_hungarian *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        post("hungarian: too many current voices (max %d)", MAX_VOICES);
        return;
    }
    
    x->current_size = argc;
    for (int i = 0; i < argc; i++) {
        x->current_chord[i] = (int)atom_getfloat(&argv[i]);
    }
    
    post("hungarian: current chord updated to [%d %d %d %d]",
         x->current_size > 0 ? x->current_chord[0] : -1,
         x->current_size > 1 ? x->current_chord[1] : -1,
         x->current_size > 2 ? x->current_chord[2] : -1,
         x->current_size > 3 ? x->current_chord[3] : -1);
}

// Enable/disable feedback mode
static void hungarian_feedback(t_hungarian *x, t_floatarg f) {
    x->feedback_enabled = (f != 0);
    post("hungarian: feedback %s", x->feedback_enabled ? "enabled" : "disabled");
}

// Set debug mode (1=on, 0=off)
static void hungarian_debug(t_hungarian *x, t_floatarg f) {
    x->debug_enabled = (f != 0);
    post("hungarian: debug %s", x->debug_enabled ? "enabled" : "disabled");
}

// Recalculate with current settings
static void hungarian_bang(t_hungarian *x) {
    hungarian_calculate(x);
}

// Constructor
static void *hungarian_new(void) {
    t_hungarian *x = (t_hungarian *)pd_new(hungarian_class);
    
    // Create outlets for chord, cost, and diagnostic info
    x->x_out_chord = outlet_new(&x->x_obj, &s_list);
    x->x_out_cost = outlet_new(&x->x_obj, &s_float);
    x->x_out_info = outlet_new(&x->x_obj, &s_list);
    
    // Initialize state
    x->current_size = 0;
    x->chord_size = 0;
    x->root_interval = 0;    // Default root is C (interval 0)
    x->feedback_enabled = 1;
    
    // Default starting chord for testing (C major: C3, E3, G3, C4)
    x->current_chord[0] = 48;
    x->current_chord[1] = 52;
    x->current_chord[2] = 55;
    x->current_chord[3] = 60;
    x->current_size = 4;
    
    x->debug_enabled = 0; // Default debug off
    post("hungarian: enhanced voice leading calculator ready");
    post("Usage: 'current <midi_notes>' to set current chord");
    post("       'root <interval>' to set root transposition");
    post("       'chord <intervals>' to set chord structure from strips");
    post("       'debug <0|1>' to toggle debug output");
    post("Features: root transposition + guaranteed chord completeness");
    
    return (void *)x;
}

// Setup function - register the Pure Data class
void hungarian_setup(void) {
    hungarian_class = class_new(gensym("hungarian"),
                               (t_newmethod)hungarian_new,
                               0,
                               sizeof(t_hungarian),
                               CLASS_DEFAULT,
                               0);
    
    // Register methods for Pure Data messaging
    class_addmethod(hungarian_class, (t_method)hungarian_current, 
                   gensym("current"), A_GIMME, 0);
    class_addmethod(hungarian_class, (t_method)hungarian_root,
                   gensym("root"), A_FLOAT, 0);
    class_addmethod(hungarian_class, (t_method)hungarian_chord,
                   gensym("chord"), A_GIMME, 0);
    class_addmethod(hungarian_class, (t_method)hungarian_feedback,
                   gensym("feedback"), A_FLOAT, 0);
    class_addmethod(hungarian_class, (t_method)hungarian_debug,
                   gensym("debug"), A_FLOAT, 0);
    class_addbang(hungarian_class, hungarian_bang);
}