/*
 * Copyright (C) 2007-2014, the BAT core developer team
 * All rights reserved.
 *
 * For the licensing terms see doc/COPYING.
 * For documentation see http://mpp.mpg.de/bat
 */

// ---------------------------------------------------------

#include "BCEfficiencyFitter.h"

#include <TF1.h>
#include <TGraph.h>
#include <TGraphAsymmErrors.h>
#include <TH1D.h>
#include <TH2D.h>
#include <TLegend.h>
#include <TMath.h>
#include <TPad.h>
#include <TRandom3.h>
#include <TString.h>

#include <BAT/BCDataSet.h>
#include <BAT/BCDataPoint.h>
#include <BAT/BCH1D.h>
#include <BAT/BCLog.h>
#include <BAT/BCMath.h>

// ---------------------------------------------------------
BCEfficiencyFitter::BCEfficiencyFitter(std::string name)
    : BCFitter(name),
      fHistogram1(0),
      fHistogram2(0),
      fHistogramBinomial(0),
      fDataPointType(kDataPointSmallestInterval)
{
    // set default options and values
    fFlagIntegration = false;

    // set MCMC for marginalization
    SetMarginalizationMethod(BCIntegrate::kMargMetropolis);
}

// ---------------------------------------------------------
BCEfficiencyFitter::BCEfficiencyFitter(TH1D* hist1, TH1D* hist2, TF1* func, std::string name)
    : BCFitter(func, name),
      fHistogram1(0),
      fHistogram2(0),
      fHistogramBinomial(0),
      fDataPointType(kDataPointSmallestInterval)
{
    SetHistograms(hist1, hist2);

    fFlagIntegration = false;

    // set MCMC for marginalization
    SetMarginalizationMethod(BCIntegrate::kMargMetropolis);
}

// ---------------------------------------------------------
bool BCEfficiencyFitter::SetHistograms(TH1D* hist1, TH1D* hist2)
{
    // check if histogram exists
    if (!hist1 or !hist2) {
        BCLog::OutError("BCEfficiencyFitter::SetHistograms : TH1D not created.");
        return false;
    }

    // check compatibility of both histograms : number of bins
    if (hist1->GetNbinsX() != hist2->GetNbinsX()) {
        BCLog::OutError("BCEfficiencyFitter::SetHistograms : Histograms do not have the same number of bins.");
        return false;
    }

    // check compatibility of both histograms : bin edges and content
    for (int i = 1; i <= hist1->GetNbinsX(); ++i) {
        if (fabs(hist1->GetBinLowEdge(i) - hist2->GetBinLowEdge(i)) > std::numeric_limits<double>::epsilon()) {
            BCLog::OutError("BCEfficiencyFitter::SetHistograms : Histogram 1 and 2 don't have same bins.");
            return false;
        }
        if (hist1->GetBinContent(i) < hist2->GetBinContent(i)) {
            BCLog::OutError("BCEfficiencyFitter::SetHistograms : Histogram 1 has fewer entries than histogram 2.");
            return false;
        }
    }
    // Check upper edge of last bin:
    if (fabs(hist1->GetXaxis()->GetXmax() - hist2->GetXaxis()->GetXmax()) > std::numeric_limits<double>::epsilon()) {
        BCLog::OutError("BCEfficiencyFitter::SetHistograms : Histogram 1 and 2 don't have same bins.");
        return false;
    }

    // set pointer to histograms
    fHistogram1 = hist1;
    fHistogram2 = hist2;

    // create a data set. this is necessary in order to calculate the
    // error band. the data set contains as many data points as there
    // are bins. for now, the data points are empty.
    SetDataSet(new BCDataSet());

    // create data points and add them to the data set.
    for (int i = 0; i < fHistogram1->GetNbinsX(); ++i)
        GetDataSet()->AddDataPoint(BCDataPoint(2));

    // set the data boundaries for x and y values.
    GetDataSet()->SetBounds(0, fHistogram1->GetXaxis()->GetXmin(), fHistogram1->GetXaxis()->GetXmax());
    GetDataSet()->SetBounds(1, 0.0, 1.0);

    // set the indeces for fitting.
    SetFitFunctionIndices(0, 1);

    // no error
    return true;
}

// ---------------------------------------------------------
BCEfficiencyFitter::~BCEfficiencyFitter()
{
    delete fHistogram1;
    delete fHistogram2;
    delete fHistogramBinomial;
}

// ---------------------------------------------------------
double BCEfficiencyFitter::LogLikelihood(const std::vector<double>& params)
{
    int c = (fMCMCCurrentChain >= 0) ? fMCMCCurrentChain : 0;

    if (!fFitFunction[c] or !fHistogram1 or !fHistogram2)
        return -std::numeric_limits<double>::quiet_NaN();

    // initialize probability
    double loglikelihood = 0;

    // set the parameters of the function
    // passing the pointer to first element of the vector is
    // not completely safe as there might be an implementation where
    // the vector elements are not stored consecutively in memory.
    // however it is much faster than copying the contents, especially
    // for large number of parameters
    fFitFunction[c]->SetParameters(&params[0]);

    // get the number of bins
    int nbins = fHistogram1->GetNbinsX();

    // loop over all bins
    for (int ibin = 1; ibin <= nbins; ++ibin) {
        // get n
        int n = int(fHistogram1->GetBinContent(ibin));

        // get k
        int k = int(fHistogram2->GetBinContent(ibin));

        // get bin edges
        double xmin = fHistogram1->GetXaxis()->GetBinLowEdge(ibin);
        double xmax = fHistogram1->GetXaxis()->GetBinUpEdge(ibin);

        double eff = 0;

        if (fFlagIntegration)				// use ROOT's TH1D::Integral method
            eff = fFitFunction[c]->Integral(xmin, xmax) / (xmax - xmin);
        else												// use linear interpolation
            eff = (fFitFunction[c]->Eval(xmax) + fFitFunction[c]->Eval(xmin)) / 2.;

        // get the value of the Poisson distribution
        loglikelihood += BCMath::LogApproxBinomial(n, k, eff);
    }

    return loglikelihood;
}

// ---------------------------------------------------------
double BCEfficiencyFitter::FitFunction(const std::vector<double>& x, const std::vector<double>& params)
{
    int c = (fMCMCCurrentChain >= 0) ? fMCMCCurrentChain : 0;
    // set the parameters of the function
    fFitFunction[c]->SetParameters(&params[0]);
    // and evaluate
    return fFitFunction[c]->Eval(x[0]);
}

// ---------------------------------------------------------
bool BCEfficiencyFitter::Fit(TH1D* hist1, TH1D* hist2, TF1* func)
{
    // set histogram
    if (hist1 and hist2)
        SetHistograms(hist1, hist2);
    else {
        BCLog::OutError("BCEfficiencyFitter::Fit : Histogram(s) not defined.");
        return false;
    }

    // set function
    if (func)
        SetFitFunction(func);
    else {
        BCLog::OutError("BCEfficiencyFitter::Fit : Fit function not defined.");
        return false;
    }

    return Fit();
}

// ---------------------------------------------------------
bool BCEfficiencyFitter::Fit()
{
    // set histogram
    if (!fHistogram1 or !fHistogram2) {
        BCLog::OutError("BCEfficiencyFitter::Fit : Histogram(s) not defined.");
        return false;
    }

    // set function
    if (fFitFunction.empty()) {
        BCLog::OutError("BCEfficiencyFitter::Fit : Fit function not defined.");
        return false;
    }

    // perform marginalization
    MarginalizeAll();

    // maximize posterior probability, using the best-fit values close
    // to the global maximum from the MCMC
    BCIntegrate::BCOptimizationMethod method_temp = GetOptimizationMethod();
    SetOptimizationMethod(BCIntegrate::kOptMinuit);
    FindMode(GetGlobalMode());
    SetOptimizationMethod(method_temp);

    // calculate the p-value using the fast MCMC algorithm
    double pvalue, pvalueCorrected;
    if ( CalculatePValueFast(GetGlobalMode(), pvalue, pvalueCorrected) )
        fPValue = pvalue;
    else
        BCLog::OutError("BCEfficiencyFitter::Fit : Could not use the fast p-value evaluation.");

    // print summary to screen
    PrintShortFitSummary();

    // no error
    return true;
}

// ---------------------------------------------------------
void BCEfficiencyFitter::DrawFit(const char* options, bool flaglegend)
{
    if (!fHistogram1 or !fHistogram2) {
        BCLog::OutError("BCEfficiencyFitter::DrawFit : Histogram(s) not defined.");
        return;
    }

    if (fFitFunction.empty()) {
        BCLog::OutError("BCEfficiencyFitter::DrawFit : Fit function not defined.");
        return;
    }

    // create efficiency graph
    TGraphAsymmErrors* histRatio = new TGraphAsymmErrors();
    histRatio->SetMarkerStyle(20);
    histRatio->SetMarkerSize(1.5);

    // set points
    for (int i = 1; i <= fHistogram1->GetNbinsX(); ++i) {
        // calculate central value and uncertainties
        double xexp, xmin, xmax;
        GetUncertainties( int(fHistogram1->GetBinContent(i)), int(fHistogram2->GetBinContent(i)), 0.68, xexp, xmin, xmax);
        histRatio->SetPoint(i - 1, fHistogram1->GetBinCenter(i), xexp);
        histRatio->SetPointError(i - 1, 0, 0, xexp - xmin, xmax - xexp);
    }

    // check wheather options contain "same"
    TString opt = options;
    opt.ToLower();

    // if not same, draw the histogram first to get the axes
    if (!opt.Contains("same")) {
        // create new histogram
        TH2D* hist_axes = new TH2D("hist_axes",
                                   Form(";%s;ratio", fHistogram1->GetXaxis()->GetTitle()),
                                   fHistogram1->GetNbinsX(),
                                   fHistogram1->GetXaxis()->GetBinLowEdge(1),
                                   fHistogram1->GetXaxis()->GetBinLowEdge(fHistogram1->GetNbinsX() + 1),
                                   1, 0., 1.);
        hist_axes->SetStats(kFALSE);
        hist_axes->Draw();

        histRatio->Draw(Form("%sp", opt.Data()));
    }

    // draw the error band as central 68% probability interval
    fErrorBand = GetErrorBandGraph(0.16, 0.84);
    fErrorBand->Draw("f same");

    // now draw the histogram again since it was covered by the band
    histRatio->SetMarkerSize(.7);
    histRatio->Draw(Form("%ssamep", opt.Data()));

    // draw the fit function on top
    fGraphFitFunction = GetFitFunctionGraph();
    fGraphFitFunction->SetLineColor(kRed);
    fGraphFitFunction->SetLineWidth(2);
    fGraphFitFunction->Draw("l same");

    // draw legend
    if (flaglegend) {
        TLegend* legend = new TLegend(0.25, 0.75, 0.55, 0.9);
        legend->SetLineColor(0);
        legend->SetFillColor(0);
        legend->AddEntry(histRatio, "Data", "PE");
        legend->AddEntry(fGraphFitFunction, "Best fit", "L");
        legend->AddEntry(fErrorBand, "Error band", "F");
        legend->Draw();
    }
    gPad->RedrawAxis();
}

// ---------------------------------------------------------
bool BCEfficiencyFitter::CalculatePValueFast(const std::vector<double>& par, double& pvalue, double& pvalueCorrected, unsigned nIterations)
{
    //use NULL pointer for no callback
    return CalculatePValueFast(par, NULL, pvalue, pvalueCorrected, nIterations);
}

// ---------------------------------------------------------
bool BCEfficiencyFitter::CalculatePValueFast(const std::vector<double>& /*par*/, BCEfficiencyFitter::ToyDataInterface* callback, double& pvalue, double& pvalueCorrected, unsigned nIterations)
{
    // check if histogram exists
    if (!fHistogram1 or !fHistogram2) {
        BCLog::OutError("BCEfficiencyFitter::CalculatePValueFast : Histogram not defined.");
        return false;
    }

    // define temporary variables
    int nbins = fHistogram1->GetNbinsX();

    std::vector<unsigned> histogram(nbins, 0);
    std::vector<double> expectation(nbins, 0);

    double logp = 0;
    double logp_start = 0;
    int counter_pvalue = 0;

    // define starting distribution
    for (int ibin = 0; ibin < nbins; ++ibin) {
        // get bin boundaries
        double xmin = fHistogram1->GetXaxis()->GetBinLowEdge(ibin + 1);
        double xmax = fHistogram1->GetXaxis()->GetBinUpEdge(ibin + 1);

        // get the number of expected events
        double yexp = fFitFunction[0]->Integral(xmin, xmax);

        // get n
        int n = int(fHistogram1->GetBinContent(ibin));

        // get k
        int k = int(fHistogram2->GetBinContent(ibin));

        histogram[ibin]   = k;
        expectation[ibin] = n * yexp;

        // calculate p;
        logp += BCMath::LogApproxBinomial(n, k, yexp);
    }
    logp_start = logp;

    // loop over iterations
    for (unsigned iiter = 0; iiter < nIterations; ++iiter) {
        // loop over bins
        for (int ibin = 0; ibin < nbins; ++ibin) {
            // get n
            int n = int(fHistogram1->GetBinContent(ibin));

            // get k
            int k = histogram.at(ibin);

            // get expectation
            double yexp = 0;
            if (n > 0)
                yexp = expectation.at(ibin) / n;

            // random step up or down in statistics for this bin
            double ptest = fRandom.Rndm() - 0.5;

            // continue, if efficiency is at limit
            if (!(yexp > 0. or yexp < 1.0))
                continue;

            // increase statistics by 1
            if (ptest > 0 && (k < n)) {
                // calculate factor of probability
                double r = (double(n) - double(k)) / (double(k) + 1.) * yexp / (1. - yexp);

                // walk, or don't (this is the Metropolis part)
                if (fRandom.Rndm() < r) {
                    histogram[ibin] = k + 1;
                    logp += log(r);
                }
            }

            // decrease statistics by 1
            else if (ptest <= 0 && (k > 0)) {
                // calculate factor of probability
                double r = double(k) / (double(n) - (double(k) - 1)) * (1 - yexp) / yexp;

                // walk, or don't (this is the Metropolis part)
                if (fRandom.Rndm() < r) {
                    histogram[ibin] = k - 1;
                    logp += log(r);
                }
            }

        } // end of looping over bins

        //one new toy data set is created
        //call user interface to calculate arbitrary statistic's distribution
        if (callback)
            (*callback)(expectation, histogram);

        // increase counter
        if (logp < logp_start)
            counter_pvalue++;

    } // end of looping over iterations

    // calculate p-value
    pvalue = double(counter_pvalue) / double(nIterations);

    // correct for fit bias
    pvalueCorrected = BCMath::CorrectPValue(pvalue, GetNParameters(), nbins);

    // no error
    return true;
}

// ---------------------------------------------------------
bool BCEfficiencyFitter::GetUncertainties(int n, int k, double p, double& xexp, double& xmin, double& xmax)
{
    if (n == 0) {
        xexp = 0.;
        xmin = 0.;
        xmax = 0.;
        return false;
    }

    BCLog::OutDebug(Form("Calculating efficiency data-point of type %d for (n,k) = (%d,%d)", fDataPointType, n, k));

    // create a histogram with binomial distribution
    if (fHistogramBinomial)
        fHistogramBinomial->Reset();
    else
        fHistogramBinomial = new TH1D("hist_binomial", "", 1000, 0., 1.);

    // loop over bins and fill histogram
    for (int i = 1; i <= fHistogramBinomial->GetNbinsX(); ++i)
        fHistogramBinomial->SetBinContent(i, BCMath::ApproxBinomial(n, k, fHistogramBinomial->GetBinCenter(i)));
    // normalize
    fHistogramBinomial->Scale(1. / fHistogramBinomial->Integral());

    switch (fDataPointType) {

        case kDataPointRMS: {
            xexp = fHistogramBinomial->GetMean();
            double rms = fHistogramBinomial->GetRMS();
            xmin = xexp - rms;
            xmax = xexp + rms;
            BCLog::OutDebug(Form(" - mean = %f , rms = %f)", xexp, rms));
            break;
        }

        case kDataPointSmallestInterval: {
            xexp = (double)k / (double)n;
            BCH1D* fbh = new BCH1D(fHistogramBinomial);
            BCH1D::BCH1DSmallestInterval SI = fbh->GetSmallestIntervals(p);
            if (SI.intervals.empty()) {
                xmin = xmax = xexp = 0.;
            } else {
                xmin = SI.intervals.front().xmin;
                xmax = SI.intervals.front().xmax;
            }
            delete fbh;
            break;
        }

        case kDataPointCentralInterval: {
            // calculate quantiles
            int nprobSum = 3;
            double q[3];
            double probSum[3];
            probSum[0] = (1. - p) / 2.;
            probSum[1] = 0.5;
            probSum[2] = (1. + p) / 2.;

            fHistogramBinomial->GetQuantiles(nprobSum, q, probSum);

            xmin = q[0];
            xexp = q[1];
            xmax = q[2];
            break;
        }

        default: {
            BCLog::OutError("BCEfficiencyFitter::GetUncertainties - invalid DataPointType specified.");
            xmin = xmax = xexp = 0.;
        }
    }
    BCLog::OutDebug(Form(" - efficiency = %f , range (%f - %f)", xexp, xmin, xmax));

    return true;
}

// ---------------------------------------------------------
