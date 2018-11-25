/*
    Copyright (c) 2016 Juraj Major

    This file is part of LTL3TELA.

    LTL3TELA is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    LTL3TELA is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with LTL3TELA.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstring>
#include "semideterministic.hpp"

std::vector<bdd> gAlphabet; // All the combinations of atomic propositions
unsigned gnc; // Number of states of non-deterministic part of sdba

// Converts a given VWAA to sDBA;
spot::twa_graph_ptr make_semideterministic(VWAA *vwaa, std::string debug) {

    // We first transform the VWAA into spot format

    // Creating helper files and saving current stream buffer
    std::ofstream outs;
    std::streambuf *coutbuf = std::cout.rdbuf();

    // Redirecting output into helper file and printing vwaa in hoa
    outs.open ("helper.hoa", std::ofstream::trunc);
    std::cout.rdbuf(outs.rdbuf());
    vwaa->print_hoaf();
    std::cout.rdbuf(coutbuf);
    outs.close();

    // Parsing the helper, acquiring spot format
    spot::parsed_aut_ptr pvwaaptr = parse_aut("helper.hoa", spot::make_bdd_dict());
    if (pvwaaptr->format_errors(std::cerr))
        return vwaa->spot_aut;  // todo Better error message
    if (pvwaaptr->aborted)
    {
        std::cerr << "--ABORT-- read\n";
        return vwaa->spot_aut;  // todo Better error message
    }
    auto pvwaa = pvwaaptr->aut;


    // We have VWAA parsed. Now, we assign Qmays and Qmusts and remove acceptance marks

    int nq = pvwaa->num_states();

    const spot::bdd_dict_ptr& dict = pvwaa->get_dict();

    bool isqmay[nq];
    bool isqmust[nq];

    auto snvwaa = pvwaa->get_named_prop<std::vector<std::string>>("state-names");

    // We iterate over all states of the VWAA
    for (unsigned q = 0; q < nq; ++q)
    {
        if (debug == "1"){std::cout << "State: " << (*snvwaa)[q] << " (" << q << ").\n";}
        // Renaming state to its number instead of the LTL formula for later use
        (*snvwaa)[q] = std::to_string(q);

        isqmay[q] = false;
        bool thereIsALoop = false;
        // If there exists a looping, but not accepting outgoing edge, we set this state as Qmay
        for (auto& t: pvwaa->out(q))
        {
            for (unsigned d: pvwaa->univ_dests(t.dst))
            {
                if (t.src == d && t.acc.id == 0) {
                    isqmay[q] = true;
                    if (debug == "1"){std::cout << "It's Qmay. ";} // It also may be Qmust
                    thereIsALoop = true;
                    break;
                }
            }
            if (thereIsALoop){ break; }
        }

        isqmust[q] = true;
        // If we find an outgoing edge, where there is no loop, we set this state as not Qmust and break the loop
        for (auto& t: pvwaa->out(q))
        {
            thereIsALoop = false;
            for (unsigned d: pvwaa->univ_dests(t.dst))
            {
                if (t.src == d){
                    thereIsALoop = true;
                }
            }
            if (!thereIsALoop){
                isqmust[q] = false;
                if (debug == "1"){std::cout << "It's not Qmust. ";} // If this is not printed, it is Qmust
                break;
            }
        }

        // Setting acceptance

        // Since we only work with "yes / no" (Buchi) acceptance, we can use the numbers differently:
        //  {} = 0 Means edge is not accepting
        // {0} = 1 Means edge is accepting
        // {1} = 2 Means edge was accepting in vwaa, but will not be accepting in sdba
        // We remove acceptance marks from all the edges, since no edge in the nondeterministic part is accepting
        for (auto& t: pvwaa->out(q))
        {
            for (unsigned d: pvwaa->univ_dests(t.dst)) {
                if (debug == "1") {
                    std::cout << "\nEdge " << t.src << "-" << d << " label: " << t.cond << " acceptance: " << t.acc << ". ";
                }
                if (t.acc != 0) {
                    t.acc = 2;
                    if (debug == "1") { std::cout << "We set acc to " << t.acc << ". "; }
                }
            }
        }
        if (debug == "1"){std::cout << "\n";}

        // todo how to handle automata that dont accept at all? do we need to bother with them?
    }

    // In gAlphabet variable, we store the set of all edge labels
    // (not only "a", "b", but also "and"-formulae: "a&b". not "a|b".)
    // Here, we create the alphabet by adding all possible labels using power-set construction

    // pow(2, pvwaa->ap().size()) is the amount of all combinations of labels = num of letters in the final alphabet
    for (int i = 0; i < pow(2, pvwaa->ap().size()); i++){
        bdd thisbdd = bdd_true();
        // for all digits in the binary form of i
        for (int digit = 0; digit < pvwaa->ap().size(); digit++){
            if ((i & (int)pow(2, digit)) != 0){
                // add atomic proposition number digit
                thisbdd = bdd_and(thisbdd, bdd_ithvar(digit));
            } else {
                thisbdd = bdd_and(thisbdd, bdd_not(bdd_ithvar(digit)));
            }
        }
        gAlphabet.push_back(thisbdd);
    }

    if (debug == "1"){
        std::cout << "The alphabet: ";
        for (auto letter : gAlphabet){
            std::cout << letter;
        }
        std::cout << "\n";
    }

    // We now start building the SDBA by removing alternation, which gives us the final nondeterministic part
    spot::twa_graph_ptr sdba = spot::remove_alternation(pvwaa, true);

    sdba->set_buchi();
    sdba->prop_state_acc(spot::trival(false));

    // First, we check whether this automaton is not already semideterministic
    // Since our VWAA uses co-Buchi acceptation, we need to negate the edges first
    spot::twa_graph_ptr negsdba = sdba;

    if (debug == "1"){ std::cout << "Testing if automaton is not semideterministic already.\n"; }
    for (auto& nedge : negsdba->edges())
    {
        if (debug == "1"){ std::cout << "Edge" << nedge.src << "-" << nedge.dst << ", acc: " << nedge.acc; }
        if (nedge.acc != 0){
            nedge.acc = 0;
        } else {
            nedge.acc = 1;
        }
        if (debug == "1"){ std::cout << ", new acc: " << nedge.acc << "\n"; }
    }
    if (negsdba->prop_semi_deterministic())
    {
        if (debug == "1"){ std::cout << "Automaton is already semideterministic. Continuing only because of debug mode.\n"; }
        else { return negsdba; }
    }

    // Number of configurations C (states in the nondeterministic part)
    gnc = sdba->num_states();

    // Definition of the phis and Rs assigned to the states in the deterministic part, for future
    std::map<unsigned, std::set<std::string>> Rname;
    std::map<unsigned, bdd> phi1;
    std::map<unsigned, bdd> phi2;
    // State-names C are in style of "1,2,3", these represent states Q of the former VWAA configuration
    auto sn = sdba->get_named_prop<std::vector<std::string>>("state-names");
    std::set<std::string> C[gnc];

    // We mark the Rs of the states of ND part so we differ them from D part states with empty R, phi1 and phi2 later on
    for (unsigned ci = 0; ci < gnc; ++ci) {
        Rname[ci] = {"ND-part state"};
    }

    // Choosing the R

    // We go through all the states in C
    // In each one, we go through all its Q-s and build all possible R-s based on what types of states Q-s are
    // For each R - if it is a new R, we build an R-component
    for (unsigned ci = 0; ci < gnc; ++ci) {

        // We parse the statename ((*sn)[ci]) to create a set of states
        if (sn && ci < sn->size() && !(*sn)[ci].empty()) {
            std::string s = ((*sn)[ci]) + ",";
            std::string delimiter = ",";
            size_t pos = 0;
            std::string token;
            while ((pos = s.find(delimiter)) != std::string::npos) {
                token = s.substr(0, pos);
                C[ci].insert(token);
                s.erase(0, pos + delimiter.length());
            }
        } else {
            std::cout << "Wrong C state name."; // todo better error message
        }

        std::set<std::string> R;

        if (debug == "1"){std::cout << "\nChecking if this configuration contains only valid states: " << ci << "\n";}
        // Check if only states reachable from Qmays are in C. If not, this configuration can not contain an R.
        if (checkMayReachableStates(pvwaa, C[ci], R, isqmay)){  // We are using R just as a placeholder empty set here
            R.clear();
            if (debug == "1"){std::cout << "Yes! \n";}
            // We call this function to judge Q-s of this C and create R-s and R-components based on them
            createDetPart(pvwaa, ci, C[ci], C[ci], R, isqmay, isqmust, sdba, Rname, phi1, phi2, debug);
        }
    }

    if (debug == "1") { std::cout << "ND part edge acceptation correction. "; }
    for (unsigned ci = 0; ci < gnc; ++ci) {
        for (auto& t: sdba->out(ci))
        {
            for (unsigned d: pvwaa->univ_dests(t.dst)) {
                if (debug == "1") {
                    std::cout << "\nEdge " << t.src << "-" << d << " accepting labels: " << t.acc << ". ";
                }
                if (t.acc == 2) {
                    t.acc = 0;
                    if (debug == "1") { std::cout << "We set acc to " << t.acc << ". "; }
                }
            }
        }
    }

    if (debug == "1") { std::cout << "\n\n"; }

    // Call spot's merge edges function
    sdba->merge_edges();

    sdba->set_buchi();
    sdba->prop_state_acc(spot::trival(false));

    // Automaton is universal if the conjunction between the labels of two transitions leaving a state is
    // always false.
    sdba->prop_universal(spot::trival());
    // automaton is complete if for each state the union of the labels of its outgoing transitions
    // is always true.
    sdba->prop_complete(spot::trival()); // todo is this correct?

    return sdba;
}

bool checkMayReachableStates(std::shared_ptr<spot::twa_graph> vwaa, std::set<std::string> Conf,
                             std::set<std::string> Valid, bool isqmay[]){

    // We check each state whether it is Qmay. If it is, we add it to Valid with all successors
    for (auto q : Conf)
    {
        if (q.empty() || !isdigit(q.at(0))){
            if (q != "{}") {
                std::cout << "We are in BADSTATE: " << q << ". "; // todo Better error message
            }
            else {
                // This state is {} and hence is Qmust // todo Is this right?
            }
        } else {
            if (isqmay[std::stoi(q)]) {
                addToValid(vwaa, q, Valid);
            }
        }
    }

    // We check and return whether all states in Conf are valid (QMays and their successors)
    for (auto q : Conf)
    {
        if (!(Valid.find(q) != Valid.end())){
            return false;
        }
    }
    return true;
}

void addToValid(std::shared_ptr<spot::twa_graph> vwaa, std::string q, std::set<std::string> &Valid){
    Valid.insert(q);
    // We add into Valid all states reachable from q too
    for (auto &t: vwaa->out((unsigned int)std::stoi(q))) {
        for (unsigned d: vwaa->univ_dests(t.dst)) {
            // We exclude loops for effectivity, as they never need to be checked to be added again
            if (std::to_string(d) != q) {
                addToValid(vwaa, std::to_string(d), Valid);
            }
        }
    }
}

void createDetPart(std::shared_ptr<spot::twa_graph> vwaa, unsigned ci, std::set<std::string> Conf,
                   std::set<std::string> remaining, std::set<std::string> R, bool isqmay[], bool isqmust[],
                   spot::twa_graph_ptr &sdba, std::map<unsigned, std::set<std::string>> &Rname,
                   std::map<unsigned, bdd> &phi1, std::map<unsigned, bdd> &phi2,
                   std::string debug){

    // We choose first q that comes into way
    auto it = remaining.begin();
    std::string q;
    if (it != remaining.end()) q = *it;

    if (debug == "1"){std::cout << "\nFunction createDetPart\nWe chose q: " << q << ". ";}
    // Erase it from remaining as we are checking it now
    remaining.erase(q);

    // Checking state correctness
    if (q.empty() || !isdigit(q.at(0))){
        if (q != "{}") {
            std::cout << "We are in BADSTATE: " << q << ". "; // todo Better error message
        }
        else {
            //If the state is {}, we know it is Qmust.
            if (debug == "1"){std::cout << "q is {} (which is Qmust), adding to R. ";}
            if (R.count(q) == 0) {
                R.insert(q);
            }
        }
    } else {
        // If this state is Qmust, we add it (and don't have to check Qmay)
        if (isqmust[std::stoi(q)]){
            if (debug == "1"){std::cout << "It is Qmust, adding to R. ";}
            if (R.count(q) == 0) {
                R.insert(q);
            }
        } else {
            // If it is Qmay, we recursively call the function and try both adding it and not
            if (isqmay[std::stoi(q)]){
                if (debug == "1"){std::cout << "It is Qmay (and not Qmust!), adding to R";}

                // We create a new branch with new R and add the state q to this R
                std::set<std::string> Rx = R;
                if (Rx.count(q) == 0) {
                    Rx.insert(q);
                }

                if (!remaining.empty()){
                    // We run the branch that builds the R where this state is added
                    if (debug == "1"){std::cout << " and creating branch including: " << q << ".\n";}
                    createDetPart(vwaa, ci, Conf, remaining, Rx, isqmay, isqmust, sdba, Rname, phi1, phi2, debug);
                } else{
                    // If this was the last state, we have one R complete. Let's build an R-component from it.
                    if (debug == "1"){std::cout << " and this was lastx state!\n----------> \nCreate Rx comp: \n";}
                    createRComp(vwaa, ci, Conf, Rx, sdba, Rname, phi1, phi2, debug);
                    if (debug == "1"){std::cout << " \n<---------- \n";}
                }
                // We also continue this run without adding this state to R - representing the second branch
                if (debug == "1"){std::cout<< "Also continuing for not adding q to R - ";}
            }
        }
        if (debug == "1"){std::cout << "Done checking for q: " << q;}

    }
    if (debug == "1"){std::cout << "\n Is this last state? ";}
    // If this was the last state, we have this R complete. Let's build an R-component from it.
    if (remaining.empty()){
        if (debug == "1"){std::cout << " YES!  \n----------> \nCreate R comp: \n";}
        createRComp(vwaa, ci, Conf, R, sdba, Rname, phi1, phi2, debug);
        if (debug == "1"){std::cout << " \n<---------- \n";}
    } else{
        if (debug == "1"){std::cout << " NO! Check another: \n";}
        createDetPart(vwaa, ci, Conf, remaining, R, isqmay, isqmust, sdba, Rname, phi1, phi2, debug);
    }
}

void createRComp(std::shared_ptr<spot::twa_graph> vwaa, unsigned ci, std::set<std::string> Conf,
                 std::set<std::string> R, spot::twa_graph_ptr &sdba, std::map<unsigned, std::set<std::string>> &Rname,
                 std::map<unsigned, bdd> &phi1, std::map<unsigned, bdd> &phi2,
                 std::string debug){
    if (debug == "1"){
        std::cout << "\nFunction createRComp\nStates of Conf: ";
        for (auto x : Conf){
            std::cout << x << ", ";
        }
        std::cout << " States of R: ";
        for (auto y : R){
            std::cout << y << ", ";
        }
        std::cout << " Num of states of sdba: " << sdba->num_states();
        std::cout << " Go:\n";
    }

    // todo optimalization, if R is empty?

    // Note: "vwaa->num_states()-1" is the last state of the vwaa, which is always the TT state.

    // The phis of this state
    bdd p1 = bdd_false();
    bdd p2 = bdd_false();

    // First we construct the edges from C into the R component by getting the correct phi1 and phi2
    unsigned nq = vwaa->num_states();

    bdd_setvarnum(sdba->num_states());

    // For each edge label ("a,b,c", "a,b,!c", "a,!b,c"...) of the alphabet
    for (auto label : gAlphabet){

        for (unsigned q = 0; q < nq; ++q) {
            if (debug == "1") { std::cout << "\nFor label: " << label << ", checking q: " << q << ". "; }

            // To deal with phi1, q either needs to not be in R, or see below *
            if (R.find(std::to_string(q)) == R.end()) {
                if (debug == "1") { std::cout << "q is not in R. \n"; }
                // q has to be in Conf to deal with phi1
                if (Conf.find(std::to_string(q)) != Conf.end()){
                    if (debug == "1") { std::cout << "q is in Conf\n"; }
                    for (auto &t: vwaa->out(q)) {
                        for (unsigned tdst: vwaa->univ_dests(t.dst)) {
                            if (debug == "1") { std::cout << "E " << t.src << "-" << tdst << " t.cond: " << t.cond
                                                          << " label: " << label << ". \n"; }

                            // If t.cond contains this label as one of the conjunctions
                            if (bdd_implies(label, t.cond)) {
                                if (debug == "1") { std::cout << "This t.cond is the same as label. "; }

                                // Replace the edges ending in R with TT
                                if (R.find(std::to_string(tdst)) == R.end()) { // If tdst was in R, we'd add TT
                                    if (debug == "1") { std::cout << "t.dst is not in R - adding " << tdst << " to phi1\n";
                                    std::cout << "phi1 before: " << p1; }

                                    if (p1 == bdd_false()){
                                        p1 = bdd_ithvar(tdst);
                                    } else {
                                        p1 = bdd_and(p1, bdd_ithvar(tdst));
                                    }
                                    if (debug == "1") { std::cout << ", phi1 after: " << p1 << ".\n"; }

                                } else {
                                    if (debug == "1") {std::cout << "Adding true to phi1\n"; }

                                    if (p1 == bdd_false()){
                                        p1 = bdd_true();
                                    } else {
                                        p1 = bdd_and(p1, bdd_true());
                                    }
                                }
                            }
                        }
                    }
                }
            } else { // * or q must be in Conf && edge must not be accepting
                if (debug == "1") { std::cout << "q is in R. \n"; }
                for (auto &t: vwaa->out(q)) {
                    for (unsigned tdst: vwaa->univ_dests(t.dst)) {
                        if (debug == "1") { std::cout << "E " << t.src << "-" << tdst << " t.cond:" << t.cond
                                                      << " label: " << label << ". \n"; }

                        // If t.cond contains this label as one of the conjunctions
                        if (bdd_implies(label, t.cond)) {
                            if (debug == "1") { std::cout << "This t.cond is the same as label. "; }

                            if ((Conf.find(std::to_string(q)) != Conf.end()) && t.acc == 0) {  // this is a correct mod. tr.
                                if (debug == "1") { std::cout << "q is in Conf and e is not acc. \n"; }

                                // Replace the edges ending in R with TT
                                if (R.find(std::to_string(tdst)) == R.end()) { // If tdst was in R, we'd add TT
                                    if (debug == "1") { std::cout << "t.dst is not in R - adding " << tdst << " to phi1. "; }

                                    if (p1 == bdd_false()) {
                                        p1 = bdd_ithvar(tdst);
                                    } else {
                                        p1 = bdd_and(p1, bdd_ithvar(tdst));
                                    }
                                } else {
                                    if (debug == "1") {std::cout << "Adding true to phi1\n"; }

                                    if (p1 == bdd_false()){
                                        p1 = bdd_true();
                                    } else {
                                        p1 = bdd_and(p1, bdd_true());
                                    }
                                }
                            }
                        }
                    }
                }
                if (debug == "1") { std::cout << "Adding q to phi2. \n"; }
                // When q is in R, we always add it to phi2
                if (p2 == bdd_false()) {
                    p2 = bdd_ithvar(q);
                } else {
                    p2 = bdd_and(p2, bdd_ithvar(q));
                }
            }
        }

        if (debug == "1") {
            std::cout << "\nThe phis we just made: phi1: " << p1 << ", phi2: " << p2;
            std::cout << "\nAll edges of Conf before: \n";
            for (unsigned c = 0; c < sdba->num_states(); ++c) {
                for (auto i: sdba->succ(sdba->state_from_number(c))) {
                    std::cout << " Bdd of edges-label: " << i->cond() << " from " << c
                              << " to " << i->dst();
                    if (sdba->state_from_number(c) == i->dst()) { std::cout << " (loop)"; }
                    std::cout << ".\n";
                }
            }
        }

        // We need to check if this R-component state exists already
        // addedStateNum is the number of the state if it exists, else value remains as a "new state" number:
        unsigned addedStateNum = sdba->num_states();
        if (debug == "1") { std::cout << "Checking if the R-comp state exists: ";}

        for (unsigned c = 0; c < sdba->num_states(); ++c) {
            if (debug == "1") {
                std::cout << "\nTry c: " << c << " Rname: ";
                for (auto x : Rname[c]){
                    std::cout << x << ", ";
                }
                std::cout << "phi1: " << phi1[c] << ", phi2: " << phi2[c];
            }
            if (Rname[c] == R && phi1[c] == p1 && phi2[c] == p2){
                addedStateNum = c;
                if (debug == "1") { std::cout << "<- this! it exists already.";}
                break;
            }
        }
        if (debug == "1") { std::cout << "\nThe statenum is: " << addedStateNum << "\n";}

        // If the state doesn't exist yet, we create it with "sdba->num_states()-1" becoming its new number.
        if (addedStateNum == sdba->num_states()) {
            if (debug == "1"){std::cout << "\nThis state is new, creating it, with edge from C" << ci << " to C"
                                                                         << addedStateNum << " labeled " << label;}
            sdba->new_state();         // addedStateNum is now equal to sdba->num_states()-1
            Rname[sdba->num_states() - 1] = R;
            phi1[sdba->num_states() - 1] = p1;
            phi2[sdba->num_states() - 1] = p2;
            //(*(sdba->get_named_prop(<std::vector<std::string>>("state-names")))[sdba->num_states()-1] = "New"; //todo name states?
            sdba->new_edge(ci, addedStateNum, label, {});
            bdd_setvarnum(sdba->num_states());
        } else {
            bool connected = false;
            // If the state already exists, we check if there is an edge leading there from the current state
            for (auto &t: sdba->out(ci)) {
                // If there is such an edge, we add this label via "OR" to the bdd
                if (t.dst == addedStateNum) {
                    if (debug == "1") {
                        std::cout << "Adding new label to the edge under OR: " << t.src << "-" << t.dst
                                  << " bdd:" << t.cond << " label: " << label << ". \n";
                    }
                    connected = true;
                    t.cond = bdd_or(t.cond, label);
                }
            }
            // If there isn't such an edge, we create a new one
            if (!connected){
                // We connect the state to this configuration under the currently checked label
                if (debug == "1"){std::cout << "New edge from C" << ci << " to C" << addedStateNum << " labeled "
                                                                                                      << label;}
                sdba->new_edge(ci, addedStateNum, label, {});
            }
        }


        // If the state is new, add all successors of this state to the sdba and connect them
        if (addedStateNum == sdba->num_states()-1) {
            if (debug == "1"){std::cout << "\nAs the state is new, adding all succs";}
            addRCompStateSuccs(vwaa, sdba, addedStateNum, Conf, Rname, phi1, phi2, debug);
        }


        if (debug == "1") {
            std::cout << "\nAll edges of Conf after adding successors: (back in function createRComp)\n";
            for (unsigned c = 0; c < sdba->num_states(); ++c) {
                for (auto i: sdba->succ(sdba->state_from_number(c))) {
                    std::cout << " t.cond: " << i->cond() << " from " << c << " to " << i->dst();
                    if (sdba->state_from_number(c) == i->dst()) { std::cout << " (loop)"; }
                    std::cout << ".\n";
                }
            }
            std::cout << "\nlaststatenum:" << sdba->num_states()-1 << ", phi1: " << phi1[sdba->num_states()-1]
                      << "phi2: " << phi2[sdba->num_states()-1];
            std::cout << "\nEnd of run of function createRComp for this label.\n";
        }
    }
}


// Adds successors of state statenum (and their successors, recursively)
void addRCompStateSuccs(std::shared_ptr<spot::twa_graph> vwaa, spot::twa_graph_ptr &sdba, unsigned statenum,
                        std::set<std::string> Conf, std::map<unsigned, std::set<std::string>> &Rname,
                        std::map<unsigned, bdd> &phi1, std::map<unsigned, bdd> &phi2,
                        std::string debug){

    if (debug == "1"){
        std::cout << "\n\n>>>>>>  Function addRCompStateSuccs for state " << statenum << "  (Rname: ";
        for (auto x : Rname[statenum]){
            std::cout << x << ", ";
        }
        std::cout << "phi1: " << phi1[statenum] << "phi2: " << phi2[statenum] << ".)\n";
    }

    // The R and phis of the state we are adding successors of
    std::set<std::string> R = Rname[statenum];
    bdd p1 = phi1[statenum];
    bdd p2 = phi2[statenum];

    // The phis for the successor state (will reset for each added state)
    bdd succp1;
    bdd succp2;

    // For each edge label of the alphabet we compute phis of the reached state (succp1 and succp2)
    for (auto label : gAlphabet) {

        succp1 = bdd_false();
        succp2 = bdd_false();

        if (debug == "1") { std::cout << "\nWe check label: " << label << " for all q states: "; }

        // We are checking all states of q that are in phi1 or phi2
        for (unsigned q = 0; q < gnc; q++){

            if (debug == "1") { std::cout << "\nChecking q: " << q << ". "; }
            if ((bdd_implies(bdd_ithvar(q), p1)) || (bdd_implies(bdd_ithvar(q), p2))){
                if (debug == "1") { std::cout << " It implies (is in) p1 (" << p1 << ") or p2 (" << p2 << ") .\n"; }

                // If edge under label is a correct m.t., add its follower to succp1 and/or succp2

                // To deal with phis, q either needs to not be in R, or see below *
                if ((R.find(std::to_string(q)) == R.end())) {
                    if (debug == "1") { std::cout << "It's not in R. \n"; }
                    // Find the edge under "label"
                    for (auto &t: vwaa->out(q)) {
                        for (unsigned tdst: vwaa->univ_dests(t.dst)) {
                            if (debug == "1") {
                                std::cout << "E " << t.src << "-" << tdst << " t.cond:" << t.cond
                                          << " label: " << label << ". \n";
                            }
                            // If t.cond contains this label as one of the conjunctions
                            if (bdd_implies(label, t.cond)) {
                                if (debug == "1") { std::cout << "<-the label is right. "; }

                                // Add the successors of p1 and p2
                                if (bdd_implies(bdd_ithvar(q), p1)) {
                                    if (R.find(std::to_string(tdst)) == R.end()) {
                                        if (debug == "1") { std::cout << "Adding tdst (" << tdst << ") to succphi1\n"; }

                                        if (succp1 == bdd_false()){
                                            succp1 = bdd_ithvar(tdst);
                                        } else {
                                            succp1 = bdd_and(succp1, bdd_ithvar(tdst));
                                        }
                                    } else {
                                        if (debug == "1") {std::cout << "Adding true to succphi1\n"; }

                                        if (succp1 == bdd_false()){
                                            succp1 = bdd_true();
                                        } else {
                                            succp1 = bdd_and(succp1, bdd_true());
                                        }
                                    }
                                }

                                if (bdd_implies(bdd_ithvar(q), p2)) {
                                    if (debug == "1") { std::cout << "Adding tdst (" << tdst << ") to succphi2. \n"; }

                                    if (succp2 == bdd_false()) {
                                        succp2 = bdd_ithvar(tdst);
                                    } else {
                                        succp2 = bdd_and(succp2, bdd_ithvar(tdst));
                                    }
                                }
                            }
                        }
                    }
                } else { // * or q must be in Conf && edge must not be accepting
                    if (debug == "1") { std::cout << "It's in R. \n"; }
                    if (Conf.find(std::to_string(q)) != Conf.end()) {
                        // Find the edge under "label"
                        for (auto &t: vwaa->out(q)) {
                            for (unsigned tdst: vwaa->univ_dests(t.dst)) {
                                if (debug == "1") {
                                    std::cout << "Q is in Conf. E " << t.src << "-" << tdst << " t.cond:" << t.cond
                                              << " label: " << label << ". \n";
                                }
                                if (t.acc == 0) {
                                    // If t.cond contains this label as one of the conjunctions
                                    if (bdd_implies(label, t.cond)) {
                                        if (debug == "1") { std::cout << "<- not accepting and the label is right. "; }

                                        // Add the successors of p1 and p2
                                        if (bdd_implies(bdd_ithvar(q), p1)) {
                                            if (R.find(std::to_string(tdst)) == R.end()) {
                                                if (debug == "1") {std::cout << "Adding tdst (" << tdst << ") to succphi1\n"; }

                                                if (succp1 == bdd_false()){
                                                    succp1 = bdd_ithvar(tdst);
                                                } else {
                                                    succp1 = bdd_and(succp1, bdd_ithvar(tdst));
                                                }
                                            } else {
                                                if (debug == "1") {std::cout << "Adding true to succphi1\n"; }

                                                if (succp1 == bdd_false()){
                                                    succp1 = bdd_true();
                                                } else {
                                                    succp1 = bdd_and(succp1, bdd_true());
                                                }
                                            }
                                        }

                                        if (bdd_implies(bdd_ithvar(q), p2)) {
                                            if (debug == "1") { std::cout << "Adding tdst (" << tdst << ") to succphi2. \n"; }

                                            if (succp2 == bdd_false()) {
                                                succp2 = bdd_ithvar(tdst);
                                            } else {
                                                succp2 = bdd_and(succp2, bdd_ithvar(tdst));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (debug == "1") { std::cout << " Done foralling. \n"; }
        bool accepting = false;

        if (succp1 == bdd_true()) {
            // We make this the breakpoint and change succp1 and succp2 completely
            if (debug == "1") { std::cout << "Succphi1 is true, changing succp1 and 2 a lot\n"; }

            succp1 = bdd_false();

            for (unsigned q = 0; q < gnc; q++){
                if (bdd_implies(bdd_ithvar(q), succp2)) {
                    if (R.find(std::to_string(q)) == R.end()) {
                        if (debug == "1") { std::cout << "Adding q: " << q << " to succphi1\n"; }

                        if (succp1 == bdd_false()){
                            succp1 = bdd_ithvar(q);
                        } else {
                            succp1 = bdd_and(succp1, bdd_ithvar(q));
                        }
                    } else {
                        if (debug == "1") {std::cout << "Not adding q: " << q << ", but true to succphi1\n"; }

                        if (succp1 == bdd_false()){
                            succp1 = bdd_true();
                        } else {
                            succp1 = bdd_and(succp1, bdd_true());
                        }
                    }
                }
            }
            succp2 = bdd_false();

            for (auto qs : R) {

                if (succp2 == bdd_false()){
                    succp2 = bdd_ithvar(stoi(qs));
                } else {
                    succp2 = bdd_and(succp2, bdd_ithvar(stoi(qs)));
                }
            }
            accepting = true;

            if (debug == "1") {
                std::cout << "New values: succphi1: " << succp1 << "succphi2: " << succp2;
            }
        }

        // We need to check if this R-component state exists already
        // succStateNum is the number of the state if it exists, else value remains as a "new state" number:
        unsigned succStateNum = sdba->num_states();
        if (debug == "1") { std::cout << "Succstatenum = sdbanumstates: " << succStateNum; }
        bool existsAlready = false;

        for (unsigned c = 0; c < sdba->num_states(); ++c) {
            if (debug == "1") {
                std::cout << "\nTry c: " << c << " Rname: ";
                for (auto x : Rname[c]) {
                    std::cout << x << ", ";
                }
                std::cout << "phi1: " << phi1[c] << "phi2: " << phi2[c];
            }
            if (Rname[c] == R && phi1[c] == succp1 && phi2[c] == succp2) {
                succStateNum = c;
                existsAlready = true;
                if (debug == "1") { std::cout << " < this!"; }
                break;
            }
        }
        if (debug == "1") { std::cout << "\nSuccstatenum: " << succStateNum << "\n"; }

        // If the state doesn't exist yet, we create it with "sdba->num_states()-1" becoming its new number.
        if (succStateNum == sdba->num_states()) {   // the same as "if !existsAlready"
            sdba->new_state();         // succStateNum is now equal to sdba->num_states()-1
            Rname[succStateNum] = R;
            phi1[succStateNum] = succp1;
            phi2[succStateNum] = succp2;
            bdd_setvarnum(sdba->num_states());
            if (debug == "1") { std::cout << "This state is new. State num: " << succStateNum << ", R: ";
                for (auto x : R){
                    std::cout << x << ", ";
                }
                std::cout << "succp1: " << succp1 << "succp2: " << succp2 << " sdba num states: "
                                                                             << sdba->num_states() << "\n";
            }
            if (debug == "1") { std::cout << "Also creating edge from C" << statenum << " to C"
                                          << succStateNum << " labeled " << label;}
            // (*(sdba->get_named_prop<std::vector<std::string>>("state-names")))[sdba->num_states()-1] = "New"; todo name states?
            if (accepting) {
                sdba->new_edge(statenum, succStateNum, label, {0});
                if (debug == "1") { std::cout << ", acc {0}. "; }
            } else {
                sdba->new_edge(statenum, succStateNum, label, {});
                if (debug == "1") { std::cout << ", acc {}. "; }
            }
         } else {
            bool connected = false;
            if (debug == "1") { std::cout << "This state exists, checking if this edge is new\n"; }
            // If the state already exists, we check if there is a same-acc edge leading there from the current state
            for (auto &t: sdba->out(statenum)) {
                // If there is such an edge, we add it as OR to the existing edge instead of creating a new edge
                if (t.dst == succStateNum && (((t.acc != 0 && accepting) || (t.acc == 0 && !accepting)))){
                    if (debug == "1") { std::cout << "Adding new label to the edge under OR: " << t.src << "-" << t.dst
                              << " bdd:" << t.cond << " bddithvarlabel" << label << ". \n"; }
                    connected = true;
                    t.cond = bdd_or(t.cond, label);
                }
            }
            // If there isn't such an edge, we create a new one
            if (!connected){
                if (accepting) {
                    if (debug == "1") { std::cout << "There is not an edge " << statenum << "-" << succStateNum
                        << " under label " << label << ", acc {0}. Creating it.\n"; }
                    sdba->new_edge(statenum, succStateNum, label, {0});
                } else {
                    if (debug == "1") { std::cout << "There is not an edge " << statenum << "-" << succStateNum
                                                  << " under label " << label << ", acc {}. Creating it.\n"; }
                    sdba->new_edge(statenum, succStateNum, label, {});
                }
            }
        }

        // If the state is new, add all further successors of this successor to the sdba and connect them
        if (!existsAlready) {
            if (debug == "1") { std::cout << "As this state is new, we are adding it's succs.\n"; }
            if (sdba->num_states() < 50) { // todo remove limit of states!
                addRCompStateSuccs(vwaa, sdba, succStateNum, Conf, Rname, phi1, phi2, debug);
            }
        }

    }
    if (debug == "1") { std::cout << "\n<<<<<<   End of function addRCompStateSuccs of state " << statenum << "\n"; }
}