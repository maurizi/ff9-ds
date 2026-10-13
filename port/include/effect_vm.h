#ifndef EFFECT_VM_H
#define EFFECT_VM_H
unsigned int port_effect_call(unsigned int target, unsigned int a0, unsigned int a1,
                              unsigned int a2, unsigned int a3, unsigned int a4);
unsigned int port_effect_data(unsigned int address);
unsigned int port_effect_call_words(unsigned int target, const unsigned int *arguments,
                                    unsigned int count);
#endif
