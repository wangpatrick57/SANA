#include "Experiment.hpp"
#include <iostream>
#include <algorithm>
#include <sstream>
#include "../utils/utils.hpp"
#include "../utils/Timer.hpp"
#include "../measures/SymmetricSubstructureScore.hpp"
#include "../measures/EdgeCorrectness.hpp"
#include "../measures/LargestCommonConnectedSubgraph.hpp"
#include "../measures/NodeCorrectness.hpp"
#include "../measures/GoCoverage.hpp"
#include "../measures/ShortestPathConservation.hpp"
#include "../measures/InvalidMeasure.hpp"
#include "../measures/InducedConservedStructure.hpp"
#include "../measures/WeightedEdgeConservation.hpp"
#include "../measures/localMeasures/GoSimilarity.hpp"
#include "../measures/localMeasures/Importance.hpp"
#include "../measures/localMeasures/Sequence.hpp"
#include "../measures/localMeasures/NodeDensity.hpp"
#include "../measures/localMeasures/EdgeDensity.hpp"
#include "../measures/localMeasures/Graphlet.hpp"
#include "../measures/localMeasures/GraphletLGraal.hpp"
#include "../Alignment.hpp"
#include "../utils/Timer.hpp"
#include "ClusterMode.hpp"


const string Experiment::methodArgsFile = "experiments/methods.cnf";
const string Experiment::datasetsFile = "experiments/datasets.cnf";

void Experiment::run(ArgumentParser& args) {
    if (args.bools["-dbg"]) {
        updateTopSeqScoreTable();
        return;
    }

    checkFileExists(methodArgsFile);
    checkFileExists(datasetsFile);
    experName = args.strings["-experiment"];
    experFolder = "experiments/"+experName+"/";
    experFile = experFolder+experName+".exp";
    checkFileExists(experFile);


    plainResultsFile = experFolder+experName+"PlainResults.txt";
    humanReadableResultsFile = experFolder+experName+"HumanReadableResults.txt";
    forPlotResultsFile = experFolder+experName+"ForPlotResults.csv";

    initSubfolders();
    initData();

    if (args.bools["-updatecsv"]) {
        loadResultMap(args.strings["-experiment"]);
        saveResultsForPlots();
        saveGoTermAveragesCSV();
        return;
    }

    bool collect = args.bools["-collect"] or allRunsFinished();
    if (collect) {
        collectResults();
        saveResults();
        return;
    }

    bool dbg = args.bools["-dbg"];
    if (not dbg) {
        makeSubmissions();        
    } else {
        printSubmissions();
    }

}

void Experiment::initSubfolders() {
    resultsFolder = experFolder+"results/";
    scriptsFolder = experFolder+"scripts/";

    //outsFolder and errsFolder must be absolute path
    //because qsub requires it 
    string projectFolder = ClusterMode::getProjectFolder();
    outsFolder = projectFolder+experFolder+"outs/";
    errsFolder = projectFolder+experFolder+"errs/";

    createFolder(resultsFolder);
    createFolder(outsFolder);
    createFolder(errsFolder);
    createFolder(scriptsFolder);
}

void Experiment::initData() {
    vector<vector<string>> data = fileToStringsByLines(experFile);
    t = stod(data[0][0]);
    nSubs = stoi(data[1][0]);
    experArgs = data[2];
    methods = data[3];
    plotMethods = vector<string> (0);
    for (string& method : methods) {
        uint n = method.size();
        if (method[n-1] == '*') method = method.substr(0, n-1);
        else plotMethods.push_back(method);
    }
    dataset = data[4][0];
    networkPairs = getNetworkPairs(dataset);
    measures = data[5];
    plotMeasures = vector<string> (0);
    for (string& measure : measures) {
        uint n = measure.size();
        if (measure[n-1] == '*') measure = measure.substr(0, n-1);
        else plotMeasures.push_back(measure);
    }
}

void Experiment::makeSubmissions() {
    for (string method: methods) {
        for (const auto& pair: networkPairs) {
            for (uint i = 0; i < nSubs; i++) {
                string cmd = subCommand(method, pair[0], pair[1], i);
                string subId = getSubId(method, pair[0], pair[1], i);
                string resultFile = resultsFolder+subId;
                if (not fileExists(resultFile)) {
                    cerr << "SUBMIT "+subId << endl;
                    execWithoutPrintingErr(cmd);
                } else {
                    cerr << "OMIT   "+subId << endl;
                }
            }
        }
    }
}

void Experiment::printSubmissions() {
    for (string method: methods) {
        for (const auto& pair: networkPairs) {
            for (uint i = 0; i < nSubs; i++) {
                string cmd = subCommand(method, pair[0], pair[1], i);
                string subId = getSubId(method, pair[0], pair[1], i);
                string resultFile = resultsFolder+subId;
                if (not fileExists(resultFile)) {
                    cout << "SUBMIT "+subId << endl;
                } else {
                    cout << "OMIT   "+subId << endl;
                }
                cout << cmd << endl;
            }
        }
    }
}

bool Experiment::allRunsFinished() {
    for (string method: methods) {
        for (const auto& pair: networkPairs) {
            for (uint i = 0; i < nSubs; i++) {
                string subId = getSubId(method, pair[0], pair[1], i);
                string resultFile = resultsFolder+subId;
                if (not fileExists(resultFile)) {
                    return false;
                }
            }
        }
    }
    return true;
}


string Experiment::getSubId(string method, string G1Name, string G2Name, uint numSub) {
    return method+"_"+G1Name+"_"+G2Name+"_"+intToString(numSub);
}

string Experiment::subCommand(string method, string G1Name, string G2Name, uint numSub) {
    string command = "./sana -mode cluster -qmode normal";
    command += " -g1 " + G1Name + " -g2 " + G2Name;
    command += " -t " + to_string(t);
    for (string arg: getMethodArgs(method)) command += " " + arg;
    for (string arg: experArgs) command += " " + arg;

    string subId = getSubId(method, G1Name, G2Name, numSub);
    command += " -o " + resultsFolder+subId;
    command += " -qsuboutfile " + outsFolder+subId+".out";
    command += " -qsuberrfile " + errsFolder+subId+".err";
    command += " -qsubscriptfile " + scriptsFolder+subId+".sh";

    return command;
}

void Experiment::loadGraphs(map<string, Graph>& graphs) {
    cerr << "Loading graphs...";
    Timer T;
    T.start();

    for (auto pair : networkPairs) {
        for (string graphName : pair) {
            if (graphs.count(graphName) == 0) {
                graphs[graphName] = Graph::loadGraph(graphName);
            }
        }
    }
    cerr << "done ("+T.elapsedString()+")" << endl;
}

string Experiment::getResultId(string method, string G1Name, string G2Name,
    uint numSub, string measure) {
    return getSubId(method, G1Name, G2Name, numSub)+"_"+measure;
}

//use after calling collect results, which initializes 'resultMap'
double Experiment::getScore(string method, string G1Name, string G2Name,
    uint numSub, string measure) {

    string key = getResultId(method, G1Name, G2Name, numSub, measure);
    if (resultMap.count(key) == 0) {
        throw runtime_error("Score not found for "+method+" "+
            G1Name+" "+G2Name+" "+intToString(numSub)+" "+measure);
    }
    return resultMap[key];
}

double Experiment::getAverageScore(string method, string G1Name,
    string G2Name, string measure) {
    double total = 0;
    uint count = 0;
    for (uint numSub = 0; numSub < nSubs; numSub++) {
        double score = getScore(method, G1Name, G2Name, numSub, measure);
        if (score != -1) {
            total += score;
            count++;
        } 
    }
    if (count > 0) return total/count;
    return -1;
}

double Experiment::computeScore(string method, string G1Name,
        string G2Name, uint numSub, Measure* measure) {

    string subId = getSubId(method, G1Name, G2Name, numSub);
    string resultFile = resultsFolder+subId;

    bool NA = measure->getName() == "invalid" or
              not fileExists(resultFile);
    double score;
    if (NA) score = -1;
    else {
        Alignment A = Alignment::loadMapping(resultFile);
        score = measure->eval(A);
    }
    return score;
}

void Experiment::collectResults() {
    resultMap.clear(); //init the map empty

    map<string, Graph> graphs;
    loadGraphs(graphs);

    for (auto pair : networkPairs) {
        string G1Name = pair[0];
        string G2Name = pair[1];
        Graph* G1 = &graphs[G1Name];
        Graph* G2 = &graphs[G2Name];
        Timer T;
        T.start();
        cerr << "("+G1Name+", "+G2Name+") ";

        for (string measureName : measures) {
            Measure* measure = loadMeasure(G1, G2, measureName);
            for (string method : methods) {
                for (uint numSub = 0; numSub < nSubs; numSub++) {
                    string resultKey = getResultId(method, G1Name, G2Name, numSub, measureName);
                    double score = computeScore(method, G1Name, G2Name, numSub, measure);
                    resultMap[resultKey] = score;
                }
            }
            delete measure;
        }
        cerr << " ("+T.elapsedString()+")" << endl;
    }
}

//to be called after collectResults
void Experiment::saveResults() {
    savePlainResults();
    saveHumanReadableResults();
    saveResultsForPlots();
}

void Experiment::savePlainResults() {
    ofstream fout;
    fout.open(plainResultsFile.c_str());
    for (string measure : measures) {
        for (auto pair : networkPairs) {
            string G1Name = pair[0];
            string G2Name = pair[1];
            for (string method : methods) {
                for (uint numSub = 0; numSub < nSubs; numSub++) {
                    double score = getScore(method, G1Name, G2Name, numSub, measure);
                    fout << measure << " " << G1Name << " ";
                    fout << G2Name << " " << method << " ";
                    fout << numSub << " " << score << endl;
                }
            }
        }
    }
}

string doubleToPrettyString(double x) {
    stringstream ss;
    ss.setf(ios::fixed);
    ss.precision(6);
    ss << x;
    string s;
    ss >> s;
    if (s.substr(s.size()-7) == ".000000") {
        s = s.substr(0, s.size()-7);
    }
    return s;
}

void addAverageToLastRow(vector<vector<string> >& table) {
    table.push_back(vector<string> (table[0].size()));
    uint n = table.size();
    table[n-1][0] = "AVG";
    table[n-1][1] = "";
    for (uint j = 2; j < table[0].size(); j++) {
        double sum = 0;
        uint count = 0;
        for (uint i = 1; i < n-1; i++) {
            double value = stod(table[i][j]);
            if (value != -1) {
                sum += value;
                count++;
            }
        }
        if (count==0) {
            table[n-1][j] = "-1";
        } else {
            table[n-1][j] = to_string(sum/count);
        }
    }
}

struct RankComp {
    vector<double> const *scores;
    RankComp(vector<double> const *scores) {
        this->scores = scores;
    }
    bool operator() (uint i, uint j) {
        double score1 = (*scores)[i];
        double score2 = (*scores)[j];
        if (score1 == -1) return false;
        if (score2 == -1) return true;
        return score1 > score2;
    }
};

void scoresToRankings(vector<string>& row) {
    uint n = row.size()-2;
    vector<double> scores(n);
    for (uint i = 0; i < n; i++) scores[i] = stod(row[i+2]);
    vector<uint> indices(n);
    for (uint i = 0; i < n; i++) indices[i] = i;
    sort(indices.begin(), indices.end(), RankComp(&scores));
    int nextRank = 1;
    for (uint i = 0; i < n; i++) {
        if (i > 0 and
            scores[indices[i]] != scores[indices[i-1]]) {
            nextRank = i+1;
        }
        row[indices[i]+2] = intToString(nextRank);
    }
}

void Experiment::saveHumanReadableResults() {
    ofstream fout(humanReadableResultsFile.c_str());

    vector<vector<vector<string> > > tables;
    for (uint i = 0; i < measures.size(); i++) {
        vector<vector<string> > table(networkPairs.size()+1,
            vector<string> (methods.size()+2));

        fout << "Measure: " << measures[i] << endl;
        table[0][0] = "G1";
        table[0][1] = "G2";
        for (uint j = 0; j < methods.size(); j++) {
            table[0][j+2] = methods[j];
        }
        for (uint j = 0; j < networkPairs.size(); j++) {
            string G1Name = networkPairs[j][0];
            string G2Name = networkPairs[j][1];
            table[j+1][0] = G1Name;
            table[j+1][1] = G2Name;
            for (uint k = 0; k < methods.size(); k++) {
                double score =
                    getAverageScore(methods[k], G1Name, G2Name, measures[i]);
                table[j+1][k+2] = doubleToPrettyString(score);
            }
        }
        tables.push_back(table);
        addAverageToLastRow(table);
        printTable(table, 1, fout);
        fout << endl << endl;
    }

    fout << endl << "=== rankings ===" << endl;
    
    for (uint i = 0; i < measures.size(); i++) {
        vector<vector<string> > &table = tables[i];
        fout << "Measure: " << measures[i] << endl;
        for (uint j = 1; j < table.size(); j++) {
            scoresToRankings(table[j]);
        }
        addAverageToLastRow(table);
        printTable(table, 1, fout);
        fout << endl << endl;
    }

    fout.close();
}

void Experiment::saveResultsForPlots() {
    ofstream fout(forPlotResultsFile.c_str());

    //headers
    bool firstCol = true;
    for (string method : plotMethods) {
        for (string measure : plotMeasures) {
            if (firstCol) firstCol = false;
            else fout << ",";
            fout << experName+"_"+method+"_"+measure;
        }
    }
    fout << endl;

    //data, one row per network pair
    for (auto pair : networkPairs) {
        string G1Name = pair[0];
        string G2Name = pair[1];
        firstCol = true;
        for (string method : plotMethods) {
            for (string measure : plotMeasures) {
                double score = getAverageScore(method, G1Name, G2Name, measure);
                if (score < 0 or score > 1) {
                    throw runtime_error("invalid score: "+to_string(score));
                }
                if (firstCol) firstCol = false;
                else fout << ",";
                fout << to_string(score);
            }
        }
        fout << endl;
    }

    //averages
    firstCol = true;
    for (string method : plotMethods) {
        for (string measure : plotMeasures) {
            double scoreSum = 0;
            for (auto pair : networkPairs) {
                string G1Name = pair[0];
                string G2Name = pair[1];
                double score = getAverageScore(method, G1Name, G2Name, measure);
                scoreSum += score;
            }
            if (firstCol) firstCol = false;
            else fout << ",";
            fout << scoreSum/networkPairs.size();
        }
    }
    fout << endl;
}

string Experiment::getName() {
    return "Experiment";
}

Experiment::Experiment() {

}

vector<string> Experiment::getMethodArgs(string method) {
    vector<vector<string>> data = fileToStringsByLines(methodArgsFile);
    for (uint i = 0; i < data.size(); i++) {
        if (data[i][0] == method) {
            data[i].erase(data[i].begin());
            return data[i];
        }
    }
    throw runtime_error("method not found in "+methodArgsFile+": "+method);
}

vector<vector<string>> Experiment::getNetworkPairs(string dataset) {
    vector<vector<string>> data = fileToStringsByLines(datasetsFile);
    for (uint i = 0; i < data.size(); i++) {
        if (data[i].size() == 1 and data[i][0] == dataset) {
            vector<vector<string>> res(0);
            uint j = i+1;
            while (j < data.size() and data[j].size() == 2) {
                res.push_back(data[j]);
                j++;
            }
            return res;
        }
    }
    throw runtime_error("dataset not found in "+datasetsFile+": "+dataset);
}

Measure* Experiment::loadMeasure(Graph* G1, Graph* G2, string name) {
    if (name == "s3") {
        return new SymmetricSubstructureScore(G1, G2);
    }
    if (name == "ec") {
        return new EdgeCorrectness(G1, G2);
    }
    if (name == "ics") {
        return new InducedConservedStructure(G1, G2);
    }
    if (name == "lccs") {
        return new LargestCommonConnectedSubgraph(G1, G2);
    }
    if (name == "noded") {
        cerr << "Warning: the weights of 'noded' might be ";
        cerr << "different than the ones used in the experiment" << endl;
        return new NodeDensity(G1, G2, {0.1, 0.25, 0.5, 0.15});
    }
    if (name == "edged") {
        cerr << "Warning: the weights of 'edged' might be ";
        cerr << "different than the ones used in the experiment" << endl;
        return new EdgeDensity(G1, G2, {0.1, 0.25, 0.5, 0.15});
    }
    if (name == "graphlet") {
        return new Graphlet(G1, G2);
    }
    if (name == "graphletlgraal") {
        return new GraphletLGraal(G1, G2);
    }
    if (name == "wecgraphletlgraal") {
        LocalMeasure* wecNodeSim = new GraphletLGraal(G1, G2);
        return new WeightedEdgeConservation(G1, G2, wecNodeSim);
    }
    if (name == "wecnoded") {
        cerr << "Warning: the weights of 'noded' might be ";
        cerr << "different than the ones used in the experiment" << endl;
        LocalMeasure* wecNodeSim = new NodeDensity(G1, G2, {0.1, 0.25, 0.5, 0.15});
        return new WeightedEdgeConservation(G1, G2, wecNodeSim);
    }
    if (name.size() == 3 and name[0] == 'g' and name[1] == 'o' and
        name[2] >= '1' and name[2] <= '9') {
        if (GoSimilarity::fulfillsPrereqs(G1, G2)) {
            cerr << "Warning: the fraction of kept GO terms might be ";
            cerr << "different than the ones used in the experiment" << endl;
            uint k = name[2] - '0';        
            vector<double> weights(k, 0);
            weights[k-1] = 1;
            return new GoSimilarity(G1, G2, weights, 0.5);
        } else {
            return new InvalidMeasure();
        }            
    }
    if (name == "gocov") {
        if (GoSimilarity::fulfillsPrereqs(G1, G2)) {
            return new GoCoverage(G1, G2);
        } else {
            return new InvalidMeasure();
        }
    }
    if (name == "shortestpath") {
        return new ShortestPathConservation(G1, G2);
    }
    if (name == "nc") {
        if (NodeCorrectness::fulfillsPrereqs(G1, G2)) {
            return new NodeCorrectness(Alignment::correctMapping(*G1, *G2));            
        } else {
            return new InvalidMeasure();
        }
    }
    if (name == "importance") {
        if (Importance::fulfillsPrereqs(G1, G2)) {
            return new Importance(G1, G2);
        }
        return new InvalidMeasure();
    }
    if (name == "sequence") {
        if (Sequence::fulfillsPrereqs(G1, G2)) {
            return new Sequence(G1, G2);  
        }
    }
    throw runtime_error("Unknown measure: "+name);
}




/////////////////////


void Experiment::saveGoTermAveragesCSV() {
    vector<string> goMeasures = {"go1","go2","go3","go4","go5","go6","go7","go8","go9"};

    map<string, double> averages;

    for (string method : plotMethods) {
        for (string measure : goMeasures) {
            double avg = 0;
            for (auto pair : networkPairs) {
                avg += getAverageScore(method, pair[0], pair[1], measure);
            }
            avg /= networkPairs.size();
            averages[method+"_"+measure] = avg;
        }
    }

    string goMeasuresFile = experFolder+experName+"GoTerms.csv";
    ofstream fout(goMeasuresFile);

    //headers
    bool firstCol = true;
    for (string method : plotMethods) {
        if (firstCol) firstCol = false;
        else fout << ",";
        fout << experName+"GoTerms_"+method;
    }
    fout << endl;
    //data
    for (string measure : goMeasures) {
        firstCol = true;
        for (string method : plotMethods) {
            if (firstCol) firstCol = false;
            else fout << ",";
            fout << averages[method+"_"+measure];
        }
        fout << endl;
    }
    //averages
    firstCol = true;
    for (string method : plotMethods) {
        double avg = 0;
        for (string measure : goMeasures) {
            avg += averages[method+"_"+measure];
        }
        avg /= goMeasures.size();
        if (firstCol) firstCol=false;
        else fout << ",";
        fout << avg;
    }
    fout << endl;
}


/////////////////



void Experiment::initResults() {
    resultMap.clear(); //init the map empty
    vector<vector<string>> data = fileToStringsByLines(plainResultsFile);
    for (vector<string> line : data) {
        string method = line[3];
        string G1Name = line[1];
        string G2Name = line[2];
        string measure = line[0];
        uint numSub = stoi(line[4]);
        string resultKey = getResultId(method, G1Name, G2Name, numSub, measure);
        double score = stod(line[5]);
        resultMap[resultKey] = score;
    }
}

void Experiment::updateTopSeqScoreTableEntry(string method,
    string G1Name, string G2Name, double topScore,
    double seqScore, string updateType) {

    string topSeqScoreTableFile = "topologySequenceScoreTable.cnf";

    vector<vector<string>> table = fileToStringsByLines(topSeqScoreTableFile);
    bool found = false;
    for (vector<string>& line : table) {
        if (line[0] == method and line[1] == G1Name and line[2] == G2Name) {
            found = true;
            if (updateType == "top") {
                line[3] = to_string(topScore);
            }
            else if (updateType == "seq") line[4] = to_string(seqScore);
            else throw runtime_error("invalid update type: "+updateType);
        }
    }
    if (not found) {
        vector<string> newLine(5);
        newLine[0] = method;
        newLine[1] = G1Name;
        newLine[2] = G2Name;
        if (updateType == "top") {
            newLine[3] = to_string(topScore);
            newLine[4] = "NA";
        } else if (updateType == "seq") {
            newLine[3] = "NA";
            newLine[4] = to_string(seqScore);
        } else throw runtime_error("invalid update type: "+updateType);
        table.push_back(newLine);
    }
    ofstream fout(topSeqScoreTableFile.c_str());
    for (vector<string> line : table) {
        fout<<line[0]<<" "<<line[1]<<" "<<line[2]<<" "<<line[3]<<" "<<line[4]<<endl;
    }
}

void Experiment::updateSeqEntriesTopSeqScoreTable() {
    for (string method : methods) {
        string renamedMethod = method;
        if (method == "sanalg") renamedMethod = "sanawec";
        for (auto pair : networkPairs) {
            string G1Name = pair[0];
            string G2Name = pair[1];
            double seqScore = getAverageScore(method, G1Name, G2Name, "sequence");
            updateTopSeqScoreTableEntry(renamedMethod, G1Name, G2Name,
                -1, seqScore, "seq");
        }
    }
}

void Experiment::updateTopEntriesTopSeqScoreTable() {
    for (string method : methods) {
        string renamedMethod = method;
        if (method == "sanalg") renamedMethod = "sanawec";
        for (auto pair : networkPairs) {
            string G1Name = pair[0];
            string G2Name = pair[1];
            string topMeasure;
            if (method == "sanaec") topMeasure = "ec";
            else if (method == "sanalg") topMeasure = "wecgraphletlgraal";
            else if (method == "sanas3") topMeasure = "s3";
            else if (method == "hubalign") topMeasure = "importance";
            else if (method == "lgraal") topMeasure = "wecgraphletlgraal";
            else if (method == "random") topMeasure = "ec";
            else throw runtime_error("unexpected method: "+method); 
            double topScore = getAverageScore(method, G1Name, G2Name, topMeasure);
            updateTopSeqScoreTableEntry(renamedMethod, G1Name, G2Name,
                topScore, -1, "top");
        }
    }
}


//the file with the plain results must exist and be complete
void Experiment::loadResultMap(string experName) {
    experFolder = "experiments/"+experName+"/";
    experFile = experFolder+experName+".exp";
    initData();
    plainResultsFile = experFolder+experName+"PlainResults.txt";
    initResults();
}

void Experiment::updateTopSeqScoreTable() {
    cerr << 0;
    Experiment exper;
    cerr << 1;
    exper.loadResultMap("biogridBeta0");
    cerr << 2;
    exper.updateTopEntriesTopSeqScoreTable();
    cerr << 3;
    exper.loadResultMap("biogridBeta1");
    cerr << 4;
    exper.updateSeqEntriesTopSeqScoreTable();
    cerr << 5;
}