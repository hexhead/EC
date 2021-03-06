int main(int argc, char** argv) {

  /// Use PLINK and tab-delimited formats to compare results.
  vector<string> datasetNames;
  datasetNames.push_back("example4.ped");
  datasetNames.push_back("example4.bed");
  datasetNames.push_back("example3.txt");

  /// Loop through the data set types.
  vector<string>::const_iterator fileIt = datasetNames.begin();
  for(; fileIt != datasetNames.end(); ++fileIt) {

    /// Create a pointer to a data set object by calling the library function
    /// to determine the data set class to use by its filename extension.
    Dataset* example4Dataset = ChooseSnpsDatasetByExtension(*fileIt);
    vector<string> ids;
    if(!example4Dataset->LoadDataset(*fileIt, false, "", "", ids)) {
      cerr << "ERROR: Could not load data set." << endl;
      exit(1);
    }

    /// Loop through the attribute values to get the mutation types.
    cout << endl << "Dataset: " << *fileIt
            << ", attribute mutation types:" << endl;
    for(unsigned int attributeIndex=0;
        attributeIndex < example4Dataset->NumAttributes();
        ++attributeIndex) {

      /// Print the attribute's mutation type.
      AttributeMutationType attributeMutationType =
        example4Dataset->GetAttributeMutationType(attributeIndex);
        cout << "Attribute Index: " << attributeIndex 
                << ", Mutation type: ";
        string mutationTypeDesc = "Unknown";
        if(attributeMutationType == TRANSITION_MUTATION) {
          mutationTypeDesc = "Transition";
        }
        if(attributeMutationType == TRANSVERSION_MUTATION) {
          mutationTypeDesc = "Transversion";
        }
        cout << mutationTypeDesc << endl;
    }

    /// Clean up dynamically allocated memory for the data set.
    delete example4Dataset;
  }

  return 0;
}

