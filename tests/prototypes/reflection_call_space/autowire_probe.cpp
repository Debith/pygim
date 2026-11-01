#include "autowire_model.h"

int main() {
    call_space::emit<autowire_target(), autowire_model>(std::cout);
}
