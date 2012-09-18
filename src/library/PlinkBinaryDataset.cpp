/*
 * PlinkBinaryDataset.cpp - Bill White - 3/10/11
 *
 * Collection class holding DatasetInstance from Plink binary format
 */

#include <string>
#include <iostream>
#include <fstream>
#include <vector>

#include <time.h>
#include <sstream>

#include <boost/lexical_cast.hpp>

#include "Dataset.h"
#include "DatasetInstance.h"
#include "StringUtils.h"
#include "PlinkBinaryDataset.h"
#include "Insilico.h"

using namespace std;
using namespace boost;
using namespace insilico;

/*****************************************************************************
 * Method: (constructor)
 *
 * IN:  nothing
 * OUT: nothing
 *
 * Constructs a plink binary dataset object.
 ****************************************************************************/
PlinkBinaryDataset::PlinkBinaryDataset() : Dataset::Dataset() {
  numInstancesRead = 0;
  numAttributesRead = 0;
  numClassesRead = 0;
  filenameBase = "";
  missingAttributeValuesToCheck.push_back("00");
  missingClassValuesToCheck.push_back("0");
  missingClassValuesToCheck.push_back("-9");
}

// -----------------------------------------------------------------------------
bool PlinkBinaryDataset::LoadSnps(string filename) {

  snpsFilename = filename;

  cout << Timestamp() << "PlinkBinaryDataset loading" << endl;

  // get the base filename
  // filenameBase = GetFullFilenameWithoutExtension(snpsFilename);
  filenameBase = GetFileBasename(filename);
  cout << Timestamp() << "Plink filename prefix for bim and bed files: "
          << filenameBase << endl;

  // ---------------------------------------------------------------------------
  // read bim file
  // sets numAttributesRead
  if(!ReadBimFile(filenameBase + ".bim")) {
    return false;
  }

  // ---------------------------------------------------------------------------
  // read fam file:
  // sets classIndexes
  // sets numInstancesRead
  // sets numClassesRead
  // sets instances vector to new DatasetInstance pointers with class set
  if(!ReadFamFile(filenameBase + ".fam")) {
    return false;
  }

  // ---------------------------------------------------------------------------
  // resize all attribue properties vectors to needed size and to allow
  // operator [] indexing
  levelCounts.resize(numAttributesRead);
  levelCountsByClass.resize(numAttributesRead);
  attributeLevelsSeen.resize(numAttributesRead);

  // add all possible genotypes to level counts
  for(unsigned int i=0; i < numAttributesRead; ++i) {
    levelCounts[i][0] = 0;
    levelCounts[i][1] = 0;
    levelCounts[i][2] = 0;
  }
  // preallocate all instance attributes
  for(unsigned int i = 0; i < instances.size(); ++i) {
    instances[i]->attributes.resize(numAttributesRead);
  }

  attributeAlleleCounts.resize(numAttributesRead);
  attributeMinorAllele.resize(numAttributesRead);
  genotypeCounts.resize(numAttributesRead);

  // ---------------------------------------------------------------------------
  // read attribute values from the bed file
  // this is a binary ccompressed format see:
  // http://pngu.mgh.harvard.edu/~purcell/plink/binary.shtml
  string bedFilename = filenameBase + ".bed";
  ifstream bedDataStream(bedFilename.c_str(), ios::in | ios::binary);
  if(!bedDataStream.is_open()) {
    cerr << "ERROR: Could not open plink bed file: " << bedFilename << endl;
    return false;
  }
  cout << Timestamp() << "Reading plink attribute data from "
          << bedFilename << endl;

  // read file magic number
  char magicNumberBuffer[2];
  bedDataStream.read(magicNumberBuffer, 2);
  string magicBits1 = get_bits(magicNumberBuffer[0]);
  string magicBits2 = get_bits(magicNumberBuffer[1]);

  // is this a bed file?
  if(magicBits1 != "01101100" || magicBits2 != "00011011") {
    cerr << bedFilename << " is not a valid binary Plink file" << endl;
    cerr << "ERROR: Magic number does not match expected value" << endl;
    return false;
  }

  // read attribute (SNP)- or individual(instance)-major ordering
  char majorModeBuffer[1];
  bedDataStream.read(majorModeBuffer, 1);
  string majorMode = get_bits(majorModeBuffer[0]);
  // read SNP or subject data
  if(majorMode == "00000000") {
    cout << Timestamp() << "Reading instance data in instance-major mode" << endl;
    cerr << Timestamp() << "ERROR: Plink instance-major mode is currently unsupported" << endl;
    exit(1);
    //    while(instanceIndex < numInstancesRead) {
    //      ++instanceIndex;
    //    }
  } else {
    if(majorMode == "00000001") {
      cout << Timestamp() << "Reading instance data in attribute-major mode" << endl;

      unsigned int bytesNeededForAttributeColumn = 0;
      if((numInstancesRead % 4) == 0) {
        bytesNeededForAttributeColumn = (numInstancesRead / 4);
      }
      else {
        bytesNeededForAttributeColumn = (numInstancesRead / 4) + 1;
      }
      cout << Timestamp() << "Reading " << bytesNeededForAttributeColumn
              << " bytes for each SNP column" << endl;
      char* attributeBuffer = new char[bytesNeededForAttributeColumn];
      unsigned int instanceIndex = 0;
      unsigned int byteIndex = 0;
      unsigned int alleleIndex = 7;
      unsigned int attributesRead = 0;
      unsigned int attributeColumn = 0;
      unsigned int attributesToRead = numInstancesRead * numAttributesRead;
      unsigned int tenPercentAttributes = (unsigned int) attributesToRead * 0.1;
      string genotypeByte = "";

      while(attributesRead < attributesToRead) {

        // after reading all snps for attribute index for all instances
        // preparse for the next snp column
        if(instanceIndex % numInstancesRead == 0) {
          byteIndex = 0;
          alleleIndex = 7;
          instanceIndex = 0;
          if(attributesRead) {
            ++attributeColumn;
          }
          // read the number of bytes needed for a column into a character buffer
          bedDataStream.read(attributeBuffer, bytesNeededForAttributeColumn);
        }

        // convert the current byte into a bit string
        genotypeByte = get_bits(attributeBuffer[byteIndex]);

        // lookup the alleles for this attribute's genotypes
        // ASSERT: all alleles are single characters representing only 0/1/2
        char bit1 = genotypeByte[alleleIndex--];
        char bit2 = genotypeByte[alleleIndex--];
        string binGenotype = "  ";
        binGenotype[0] = bit1;
        binGenotype[1] = bit2;
        pair<char, char> alleles = attributeAlleles[attributeColumn];
//        cout << attributeColumn << "\t" << alleles.first
//                << "\t" << alleles.second << endl;
        AttributeLevel attributeLevel = MISSING_ATTRIBUTE_VALUE;
        string stringGenotype = "  ";
        if(binGenotype == "00") {
          stringGenotype[0] = alleles.first;
          stringGenotype[1] = alleles.first;
          ++attributeAlleleCounts[attributeColumn][alleles.first];
          ++attributeAlleleCounts[attributeColumn][alleles.first];
          attributeLevel = 2;
        }
        if(binGenotype == "01") {
          stringGenotype[0] = alleles.first;
          stringGenotype[1] = alleles.second;
          ++attributeAlleleCounts[attributeColumn][alleles.first];
          ++attributeAlleleCounts[attributeColumn][alleles.second];
          attributeLevel = 1;
        }
        if(binGenotype == "11") {
          stringGenotype[0] = alleles.second;
          stringGenotype[1] = alleles.second;
          ++attributeAlleleCounts[attributeColumn][alleles.second];
          ++attributeAlleleCounts[attributeColumn][alleles.second];
          attributeLevel = 0;
        }
        if(binGenotype == "10") {
          attributeLevel = MISSING_ATTRIBUTE_VALUE;
        }
        if(attributeLevel != MISSING_ATTRIBUTE_VALUE) {
          ++genotypeCounts[attributeColumn][stringGenotype];
        }

//        cout << instanceIndex << "," << attributeColumn << ": "
//                << "bi:" << byteIndex << " " << genotypeByte << ", "
//                << ", " << binGenotype << " -> " << attributeLevel << endl;

        // finally, we can set the attribute value - if this instance is
        // to be included
				instances[instanceIndex]->attributes[attributeColumn] = attributeLevel;
				attributeLevelsSeen[attributeColumn].insert(stringGenotype);
        ++attributesRead;
        ++instanceIndex;

        // four genotypes per byte, ie, 2 bits per genotype
        if(instanceIndex % 4 == 0) {
          ++byteIndex;
          alleleIndex = 7;
        }

        // happy lights
        // express as a percentage rather than huge numbers
        float percentDone = ((float) attributesRead / attributesToRead) * 100.0;
        if((attributesRead % tenPercentAttributes) == 0) {
          cout << Timestamp() << (int) percentDone << "%" << endl;
        }

      } // !done
      cout << Timestamp() << "100% decoded data set" << endl;

      // release dynamically-allocated memory
      delete [] attributeBuffer;

    }// majorMode == ?
    else {
      cerr << "ERROR: Major mode " << majorMode << " is not recognized" << endl;
      return false;
    }
  }
  bedDataStream.close();

  /// Remove instances that are not in instanceIdsToLoad
  /// or marked as missing phenotype - 11/1/11
  /// Only remove missing phenotypes if no alt pheno file - 1/23/12
  vector<DatasetInstance*> newInstances;
  vector<DatasetInstance*> delInstances;
  map<string, unsigned int> keepIds;
  map<string, unsigned int>::iterator it = instancesMask.begin();
  for(; it != instancesMask.end(); ++it) {
    string instanceID = it->first;
    DatasetInstance* dsi = instances[it->second];
    bool keepInstance = true;
    if(IsLoadableInstanceID(instanceID)) {
    	if(!hasAlternatePhenotypes) {
    		if(hasContinuousPhenotypes &&
					dsi->GetPredictedValueTau() == MISSING_NUMERIC_CLASS_VALUE) {
          cout << Timestamp() << "Instance ID " << instanceID
          		<< " filtered out by missing value" << endl;
					delInstances.push_back(dsi);
					keepInstance = false;
    		}
    		if(!hasContinuousPhenotypes &&
    				dsi->GetClass() == MISSING_DISCRETE_CLASS_VALUE) {
          cout << Timestamp() << "Instance ID " << instanceID
          		<< " filtered out by missing value" << endl;
					delInstances.push_back(dsi);
					keepInstance = false;
    		}
    	}
    } else {
      cout << Timestamp() << "Instance ID " << instanceID
      		<< " filtered out by numeric or alt pheno file" << endl;
      delInstances.push_back(dsi);
      keepInstance = false;
    }
  	/// Passed all tests, so add this instance to the data set
  	if(keepInstance) {
  		keepIds[instanceID] = newInstances.size();
  		newInstances.push_back(dsi);
  	}
  }
  instances = newInstances;
  instancesMask.clear();
  instancesMask = keepIds;

  /// Release memory used by filtered out instances
  vector<DatasetInstance*>::iterator delIt = delInstances.begin();
  for(; delIt != delInstances.end(); ++delIt) {
    delete *delIt;
  }

  // refresh any instance-based data
  if(!hasContinuousPhenotypes) {
		classIndexes.clear();
		for(unsigned int instanceIdx = 0; instanceIdx < instances.size(); ++instanceIdx) {
			classIndexes[instances[instanceIdx]->GetClass()].push_back(instanceIdx);
		}
  }
  numClassesRead = classIndexes.size();
  numInstancesRead = instances.size();

  // determine minor allele and its frequency - 12/21/11
  for(unsigned int attrIdx = 0; attrIdx < NumAttributes(); ++attrIdx) {
    map<char, unsigned int> thisAttrMap = attributeAlleleCounts[attrIdx];
    if(thisAttrMap.size() != 2) {
      cerr << "ERROR: Only biallelic genotypes are supported, "
      		<< thisAttrMap.size() << " alleles detected in attribute index:"
      		<< attrIdx << endl;
      return false;
    }
    map<char, unsigned int>::const_iterator mapIt = thisAttrMap.begin();
    char allele1 = mapIt->first;
    unsigned int allele1Count = mapIt->second;
    ++mapIt;
    char allele2 = mapIt->first;
    unsigned int allele2Count = mapIt->second;
    string majorAllele = " ";
    string minorAllele = " ";
    double attributeMaf = 0.0;
    if(allele1Count < allele2Count) {
      minorAllele[0] = allele1;
      attributeMaf = ((double) allele1Count) / (NumInstances() * 2.0);
      majorAllele[0] = allele2;
    } else {
      minorAllele[0] = allele2;
      attributeMaf = ((double) allele2Count) / (NumInstances() * 2.0);
      majorAllele[0] = allele1;
    }
    //    cout << "Attribute: " << attrIdx
    //            << ", A1: " << minorAllele
    //            << ", A2: " << majorAllele
    //            << ", MAF: " << attributeMaf
    //            << endl;
    attributeMinorAllele[attrIdx] = make_pair(minorAllele[0], attributeMaf);
  }
  cout << Timestamp() << "There are " << NumInstances()
          << " instances in the data set" << endl;
  cout << Timestamp() << "There are " << instancesMask.size()
          << " instances in the instance mask" << endl;

  if(!hasContinuousPhenotypes) {
    cout << Timestamp() << "There are " << classIndexes.size()
            << " classes in the data set" << endl;
  }

  UpdateAllLevelCounts();

  hasGenotypes = true;

  hasAllelicInfo = true;

  return true;
}

// -----------------------------------------------------------------------------

bool PlinkBinaryDataset::ReadBimFile(string bimFilename) {
  // read attribute information from the bim file
  ifstream bimDataStream(bimFilename.c_str());
  if(!bimDataStream.is_open()) {
    cerr << "ERROR: Could not open plink binary bim file: " << bimFilename << endl;
    return false;
  }
  cout << Timestamp() << "Reading plink bim/attribute metadata from "
          << bimFilename << endl;
  unsigned int bimLineNumber = 0;
  string line;
  // pair < map<string, unsigned int>::iterator, bool> retAlleleInsert;
  unsigned int attrIdx = 0;
  while(getline(bimDataStream, line)) {
    ++bimLineNumber;
    string trimmedLine = trim(line);
    if(trimmedLine[0] == '#') {
      continue;
    }
    if(trimmedLine.size() == 0) {
      continue;
    }
    vector<string> tokens;
    split(tokens, trimmedLine);
    if(tokens.size() != 6) {
      cerr << "ERROR: reading plink bim file line "
              << bimLineNumber << ". "
              << "Each row should have six whitespace-separated columns"
              << endl;
      return false;
    }
    attributeNames.push_back(tokens[1]);
    attributesMask[tokens[1]] = attrIdx;
    ++attrIdx;
    string genotypeAllele1 = tokens[4];
    string genotypeAllele2 = tokens[5];
    attributeAlleles.push_back(make_pair(genotypeAllele1[0],
                                         genotypeAllele2[0]));
    /// set the mutation type
    attributeMutationTypes.push_back
            (
             attributeMutationMap[make_pair(genotypeAllele1[0],
                                            genotypeAllele2[0])]
            );

    // cout << genotypeAllele1[0] << ", " << genotypeAllele2[0] << endl;
    //    map<string, unsigned int>* map1 = new map<string, unsigned int>;
    //    map<unsigned int, string>* map2 = new map<unsigned int, string>;
    //    map<string, unsigned int> map1;
    //    map<unsigned int, string> map2;
    //    map1[genotypeAllele1] = 0;
    //    map1[genotypeAllele2] = 1;
    //    map2[0] = genotypeAllele1;
    //    map2[1] = genotypeAllele2;
    //
    //    alleleValuesByString.push_back(map1);
    //    alleleValuesByInt.push_back(map2);
  }
  bimDataStream.close();
  numAttributesRead = bimLineNumber;
  classColumn = numAttributesRead;
  cout << Timestamp() << "There are " << numAttributesRead
          << " attributes in the dataset" << endl;

  return true;
}

// -----------------------------------------------------------------------------

bool PlinkBinaryDataset::ReadFamFile(string famFilename) {

	/// Detect the class type
	bool classDetected = false;
	switch(DetectClassType(famFilename, 6, false)) {
	case CASE_CONTROL_CLASS_TYPE:
		cout << Timestamp() << "Case-control phenotypes detected" << endl;
		hasContinuousPhenotypes = false;
		classDetected = true;
		break;
	case CONTINUOUS_CLASS_TYPE:
		cout << Timestamp() << "Continuous phenotypes detected" << endl;
		hasContinuousPhenotypes = true;
		classDetected = true;
		break;
	case MULTI_CLASS_TYPE:
		cout << "ERROR: more than two discrete phenotypes detected" << endl;
		break;
	case NO_CLASS_TYPE:
		cout << Timestamp() << "NOTE: phenotypes could not be detected in FAM file"
			<< endl;
		break;
	}
	if(!classDetected && !hasAlternatePhenotypes) {
		return false;
	}

	/// Read attribute information from the fam file
  ifstream famDataStream(famFilename.c_str());
  pair < map<string, unsigned int>::iterator, bool> retClassInsert;
  if(!famDataStream.is_open()) {
    cerr << "ERROR: Could not open plink binary fam file: " << famFilename << endl;
    return false;
  }
  cout << Timestamp() << "Reading plink binary fam file from "
          << famFilename << endl;

  string line;
  int famLineNumber = 0;
  numInstancesRead = 0;
  double minPheno = 0.0, maxPheno = 0.0;
  unsigned int instanceIndex = 0;
  while(getline(famDataStream, line)) {
    ++famLineNumber;

    string trimmedLine = trim(line);
    if(trimmedLine[0] == '#') {
      continue;
    }
    if(trimmedLine.size() == 0) {
      continue;
    }
    ++numInstancesRead;

    vector<string> tokens;
    split(tokens, trimmedLine);
    // use the PLINK IID as the instance ID - 9/18/12
    string ID = tokens[1];
    string thisClassString = tokens[5];

    /// assign class level
    ClassLevel discreteClassLevel = MISSING_DISCRETE_CLASS_VALUE;
    NumericLevel numericClassLevel = MISSING_NUMERIC_CLASS_VALUE;
    if(hasContinuousPhenotypes) {
    	if(thisClassString != "-9") {
    		numericClassLevel = lexical_cast<NumericLevel>(thisClassString);
				if(famLineNumber == 1) {
					minPheno = maxPheno = numericClassLevel;
				} else {
					if(numericClassLevel < minPheno) {
						minPheno = numericClassLevel;
					}
					if(numericClassLevel > maxPheno) {
						maxPheno = numericClassLevel;
					}
				}
    	}
    }
    else {
    	if(thisClassString != "-9") {
    		discreteClassLevel = lexical_cast<ClassLevel>(thisClassString) - 1;
    	}
    }

		/// Create a new instance for this individual
    DatasetInstance* newInst = new DatasetInstance(this);
		if(hasContinuousPhenotypes) {
			newInst->SetPredictedValueTau(numericClassLevel);
		} else {
			newInst->SetClass(discreteClassLevel);
    	if(thisClassString != "-9") {
    		classIndexes[discreteClassLevel].push_back(famLineNumber - 1);
    	}
		}
		instances.push_back(newInst);
		instanceIds.push_back(ID);
		instancesMask[ID] = instanceIndex;

		++instanceIndex;
  }
  famDataStream.close();

  if(hasContinuousPhenotypes) {
    continuousPhenotypeMinMax = make_pair(minPheno, maxPheno);
  }

  cout << Timestamp() << numInstancesRead << " individuals read from the "
  		<< "fam file." << endl;

  return true;
}

pair<char, double> PlinkBinaryDataset::GetAttributeMAF(unsigned int attributeIndex) {
  pair<char, double> returnPair = make_pair(' ', 0.0);
  if(attributeIndex < NumAttributes()) {
    returnPair = attributeMinorAllele[attributeIndex];
  }
  return returnPair;
}

AttributeMutationType PlinkBinaryDataset::GetAttributeMutationType(unsigned int attributeIndex) {
  AttributeMutationType returnType = UNKNOWN_MUTATION;
  if(attributeIndex < NumAttributes()) {
    returnType = attributeMutationTypes[attributeIndex];
  }
  return returnType;
}
