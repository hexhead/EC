/* 
 * File:   RandomJungle.cpp
 * Author: billwhite
 * 
 * Created on October 16, 2011, 3:45 PM
 *
 * Adapter class to map EC call for Random Jungle importance scores
 * to Random Jungle library functions.
 */

// Random Jungle source distribution
#include "rjungle/librjungle.h"
#include "rjungle/RJunglePar.h"
#include "rjungle/RJungleCtrl.h"
#include "rjungle/DataFrame.h"
#include "rjungle/FittingFct.h"
#include "rjungle/RJungleHelper.h"
#include "rjungle/Helper.h"

#include "RandomJungle.h"
#include "StringUtils.h"
#include "Insilico.h"

using namespace std;
using namespace insilico;

RandomJungle::RandomJungle(Dataset* ds, po::variables_map& vm) {
  uli_t numTrees = vm["rj-num-trees"].as<uli_t > ();
  cout << Timestamp() << "Initializing Random Jungle with " << numTrees
          << " trees" << endl;

  dataset = ds;
  
  rjParams = initRJunglePar();

  int numProcs = omp_get_num_procs();
  int numThreads = omp_get_num_threads();
  cout << Timestamp() << numProcs << " OpenMP processors available" << endl;
  cout << Timestamp() << numThreads << " OpenMP threads running" << endl;

  rjParams.mpiId = 0;
  rjParams.nthreads = numProcs;
  rjParams.verbose_flag = vm["verbose"].as<bool>();

  // fill in the parameters object for the RJ run
  rjParams.rng = gsl_rng_alloc(gsl_rng_mt19937);
  gsl_rng_set(rjParams.rng, rjParams.seed);

  if(vm.count("rj-num-trees")) {
    rjParams.ntree = vm["rj-num-trees"].as<uli_t > ();
  } else {
    rjParams.ntree = 1000;
  }

  rjParams.nrow = dataset->NumInstances();
  rjParams.depVarName = (char *) "Class";
  //  rjParams.verbose_flag = true;
  rjParams.filename = (char*) "";
  
  string outFilesPrefix = vm["out-files-prefix"].as<string>();
  rjParams.outprefix = (char*) outFilesPrefix.c_str();
}

RandomJungle::~RandomJungle() {
  if(rjParams.rng) {
    gsl_rng_free(rjParams.rng);
  }
}

bool RandomJungle::ComputeAttributeScores() {

  vector<uli_t>* colMaskVec = NULL;
  time_t start, end;
  clock_t startgrow, endgrow;

  time(&start);

  rjParams.ncol = dataset->NumVariables() + 1;
  rjParams.depVar = rjParams.ncol - 1;
  rjParams.depVarCol = rjParams.ncol - 1;
  string outPrefix(rjParams.outprefix);
  string importanceFilename = outPrefix + ".importance";

  // base classifier: classification or regression trees?
  string treeTypeDesc = "";
  if(dataset->HasContinuousPhenotypes()) {
    // regression
    if(dataset->HasNumerics() && dataset->HasGenotypes()) {
      // integrated/numeric
      rjParams.treeType = 3;
      treeTypeDesc = "Regression trees: integrated/continuous";
    }
    else {
      if(dataset->HasGenotypes()) {
        // nominal/numeric
        rjParams.treeType = 4;
        treeTypeDesc = "Regression trees: discrete/continuous";
      }
      else {
      // numeric/numeric
        if(dataset->HasNumerics()) {
          rjParams.treeType = 3;
          treeTypeDesc = "Regression trees: integrated/continuous";
        }
      }
    }
  }
  else {
    // classification
    if(dataset->HasNumerics() && dataset->HasGenotypes()) {
      // mixed/nominal
      rjParams.treeType = 1;
      treeTypeDesc = "Classification trees: integrated/discrete";
    }
    else {
      if(dataset->HasGenotypes()) {
        // nominal/nominal
        // rjParams.treeType = 2;
        // tree type 2 sucks at importance ranking
        rjParams.treeType = 2;
        treeTypeDesc = "Classification trees: discrete/discrete";
      }
      else {
      // numeric/nominal
        if(dataset->HasNumerics()) {
          rjParams.treeType = 1;
          treeTypeDesc = "Classification trees: continuous/discrete";
        }
      }
    }
  }
  cout << Timestamp() << treeTypeDesc << endl;

  RJungleIO io;
  io.open(rjParams);
  unsigned int numInstances = dataset->NumInstances();
  vector<string> variableNames = dataset->GetVariableNames();
  vector<string> instanceIds = dataset->GetInstanceIds();

//  cout << "Variables names from the data set:" << endl;
//  copy(variableNames.begin(), variableNames.end(), ostream_iterator<string>(cout, "\n"));

  vector<string> attributeNames = dataset->GetAttributeNames();
  vector<string> numericNames = dataset->GetNumericsNames();
  if((rjParams.treeType == 1) ||
     (rjParams.treeType == 3) ||
     (rjParams.treeType == 4)) {
    // regression
    cout << Timestamp() << "Preparing regression trees Random Jungle" << endl;
    rjParams.memMode = 0;
    rjParams.impMeasure = 2;
//    rjParams.backSel = 3;
//    rjParams.numOfImpVar = 2;
    DataFrame<NumericLevel>* data = new DataFrame<NumericLevel>(rjParams);
    data->setDim(rjParams.nrow, rjParams.ncol);
    variableNames.push_back(rjParams.depVarName);
    data->setVarNames(variableNames);
    data->setDepVarName(rjParams.depVarName);
    data->setDepVar(rjParams.depVarCol);
    data->initMatrix();
    // load data frame
    // TODO: do not load data frame every time-- use column mask mechanism?
    cout << Timestamp() << "Loading RJ DataFrame with double values: ";
    cout.flush();
    for(unsigned int i = 0; i < numInstances; ++i) {
      unsigned int instanceIndex;
      dataset->GetInstanceIndexForID(instanceIds[i], instanceIndex);
      unsigned int j = 0;
      for(unsigned int a = 0; a < attributeNames.size(); ++a) {
//        cout << "Loading instance " << i << ", attribute: " << a
//                << " " << attributeNames[a] << endl;
        data->set(i, j, (NumericLevel) dataset->GetAttribute(instanceIndex,
                                                             attributeNames[a]));
        ++j;
      }
      for(unsigned int n = 0; n < numericNames.size(); ++n) {
        data->set(i, j, dataset->GetNumeric(i, numericNames[n]));
        ++j;
      }
      if(dataset->HasContinuousPhenotypes()) {
        data->set(i, j, dataset->GetInstance(instanceIndex)->GetPredictedValueTau());
      }
      else {
        data->set(i, j, (double) dataset->GetInstance(instanceIndex)->GetClass());
      }
      // happy lights
      if(i && ((i % 100) == 0)) {
        cout << i << "/" << numInstances << " ";
        cout.flush();
      }
      // happy lights
      if(i && ((i % 1000) == 0)) {
        cout << endl << Timestamp();
      }
    }
    cout << numInstances << "/" << numInstances << endl;
    data->storeCategories();
    data->makeDepVecs();
    data->getMissings();
//    cout << "DEBUG data frame:" << endl;
//    data->print(cout);
//    data->printSummary();

    RJungleGen<NumericLevel> rjGen;
    rjGen.init(rjParams, *data);

    startgrow = clock();
    // create controller
    RJungleCtrl<NumericLevel> rjCtrl;
    cout << Timestamp() << "Running Random Jungle" << endl;
    rjCtrl.autoBuildInternal(rjParams, io, rjGen, *data, colMaskVec);
    endgrow = clock();

    time(&end);

    // print info stuff
    RJungleHelper<NumericLevel>::printRJunglePar(rjParams, *io.outLog);
    RJungleHelper<NumericLevel>::printFooter(rjParams, io, start, end,
                                       startgrow, endgrow);
    delete data;
  }
  else {
    cout << Timestamp() << "Preparing SNP classification trees Random Jungle" << endl;
    rjParams.memMode = 2;
    rjParams.impMeasure = 1;
    // set tree type to 1; works better with SNPs for some reason???
    rjParams.treeType = 1;
    DataFrame<char>* data = new DataFrame<char>(rjParams);
    data->setDim(rjParams.nrow, rjParams.ncol);
    variableNames.push_back(rjParams.depVarName);
    data->setVarNames(variableNames);
    data->setDepVarName(rjParams.depVarName);
    data->setDepVar(rjParams.depVarCol);
    data->initMatrix();
    // TODO: do not load data frame every time-- use column mask mechanism?
    cout << Timestamp() << "Loading RJ DataFrame values: ";
    for(unsigned int i = 0; i < rjParams.nrow; ++i) {
      unsigned int instanceIndex;
      dataset->GetInstanceIndexForID(instanceIds[i], instanceIndex);
      // set attributes from data set
      for(unsigned int j = 0; j < rjParams.ncol - 1; ++j) {
        AttributeLevel intAttr = dataset->GetAttribute(instanceIndex,
                                                       variableNames[j]);
        char attr = ' ';
        switch(intAttr) {
          case 0:
            attr = 0x0;
            break;
          case 1:
            attr = 0x1;
            break;
          case 2:
            attr = 0x2;
            break;
          case MISSING_ATTRIBUTE_VALUE:
            // Random Jungle says to avoid missing values. Duh!
            // What does this mean? Trying question mark.
            attr = '?';
            break;
        }
//        cout << "Setting (" << i << "," << j << ") => "
//                << intAttr << ", " << attr << endl;
        data->set(i, j, attr);
      }

      // set class from data set
      unsigned int intClass = dataset->GetInstance(instanceIndex)->GetClass();
      char classVal = ' ';
      if(intClass == 0) {
        classVal = 0x0;
      }
      else {
        classVal = 0x1;
      }
//      cout << "Setting class value (" << i << "," << (rjParams.ncol - 1)
//              << ") => " << intClass << ", " << classVal << endl;
      data->set(i, rjParams.ncol - 1, classVal);

      // happy lights
      if(i && ((i % 100) == 0)) {
        cout << i << "/" << numInstances << " ";
        cout.flush();
      }
      if(i && ((i % 1000) == 0)) {
        cout << i << endl << Timestamp();
      }
    }
    cout << numInstances << "/" << numInstances << endl;
    data->storeCategories();
    data->makeDepVecs();
    data->getMissings();
    // data->print(cout);

    RJungleGen<char> rjGen;
    rjGen.init(rjParams, *data);

    startgrow = clock();
    // create controller
    RJungleCtrl<char> rjCtrl;
    cout << Timestamp() << "Running Random Jungle" << endl;
    rjCtrl.autoBuildInternal(rjParams, io, rjGen, *data, colMaskVec);
    endgrow = clock();

    time(&end);

    // print info stuff
    RJungleHelper<char>::printRJunglePar(rjParams, *io.outLog);
    RJungleHelper<char>::printFooter(rjParams, io, start, end,
                                       startgrow, endgrow);
    delete data;
  }

  // clean up
  if(colMaskVec != NULL) {
    delete colMaskVec;
  }

  // clean up Random Jungle run
  io.close();

  // loads rjScores map
  cout << Timestamp() << "Loading RJ variable importance (VI) scores "
          << "from [" << importanceFilename << "]" << endl;
  if(!ReadScores(importanceFilename)) {
    cerr << "ERROR: Could not read Random Jungle scores" << endl;
    return false;
  }

  return true;
}

vector<pair<double, string> > RandomJungle::GetScores() {
  return scores;
}

bool RandomJungle::ReadScores(string importanceFilename) {
  ifstream importanceStream(importanceFilename.c_str());
  if(!importanceStream.is_open()) {
    cerr << "ERROR: Could not open Random Jungle importance file: "
            << importanceFilename << endl;
    return false;
  }
  string line;
  // strip the header line
  getline(importanceStream, line);
  // read and store variable name and gini index
  unsigned int lineNumber = 0;
  double minRJScore = 0; // initializations to keep compiler happy
  double maxRJScore = 0; // initializations are on line number 1 of file read
  scores.clear();
  while(getline(importanceStream, line)) {
    ++lineNumber;
    vector<string> tokens;
    split(tokens, line, " ");
    if(tokens.size() != 4) {
      cerr << "ERROR: EvaporativeCooling::ReadRandomJungleScores: "
              << "error parsing line " << lineNumber
              << " of " << importanceFilename
              << ". Read " << tokens.size() << " columns. Should "
              << "be 4" << endl;
      return false;
    }
    string val = tokens[2];
    string keyVal = tokens[3];
    double key = strtod(keyVal.c_str(), NULL);
    // cout << "Storing RJ: " << key << " => " << val << endl;
    scores.push_back(make_pair(key, val));
    if(lineNumber == 1) {
      minRJScore = key;
      maxRJScore = key;
    } else {
      if(key < minRJScore) {
        minRJScore = key;
      } else {
        if(key > maxRJScore) {
          maxRJScore = key;
        }
      }
    }
  }
  importanceStream.close();
  cout << Timestamp() << "Read [" << scores.size() << "] scores from ["
          << importanceFilename << "]" << endl;
  // normalize scores
  bool needsNormalization = true;
  if(minRJScore == maxRJScore) {
    cout << Timestamp() << "WARNING: Random Jungle min and max scores "
            << "are the same" << endl;
    needsNormalization = false;
  }
  double rjRange = maxRJScore - minRJScore;
  vector<pair<double, string> > newRJScores;
  for(unsigned int i = 0; i < scores.size(); ++i) {
    pair<double, string> thisScore = scores[i];
    double key = thisScore.first;
    string val = thisScore.second;
    if(needsNormalization) {
      key = (key - minRJScore) / rjRange;
      newRJScores.push_back(make_pair(key, val));
    } else {
      newRJScores.push_back(make_pair(key, val));
    }
  }

  scores.clear();
  scores = newRJScores;

  return true;
}