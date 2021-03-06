/*
    Copyright (c) 2016 Juraj Major
    Copyright (c) 2018 Michal Románek

    This file is part of LTL2SDBA.

    LTL2SDBA is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    LTL2SDBA is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with LTL2SDBA.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef ALTERATING_H
#define ALTERNATING_H
#include "utils.hpp"
#include "automaton.hpp"
#include <spot/tl/parse.hh>
#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>
#include <iostream>
#include <map>
#include <set>

// registers atomic proposition from a state formula
void register_ap_from_boolean_formula(VWAA* slaa, spot::formula f);

// checks whether U-formula f is mergeable
bool is_mergeable(VWAA* vwaa, spot::formula f);

// converts an LTL formula to self-loop alternating automaton
VWAA* make_alternating(spot::formula f);

// helper function for LTL to automata translation
unsigned make_alternating_recursive(VWAA* slaa, spot::formula f);

#endif
