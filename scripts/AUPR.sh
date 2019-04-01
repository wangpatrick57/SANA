#!/bin/sh
TMP=/tmp/AUPR.$$
trap "/bin/rm -f $TMP.*; exit" 0 1 2 3 15
USAGE="USAGE:

$0 [-A 0|1] [-R RT] [-S ST] c1 c2 TruthFile G1.el G2.el ScoreFile [resnikFile] [seqSimFile] [complexesFile]

where:

-A [0 or 1] specifies whether or to measure correctness relative to all orthologs or only those found in the scores file
RT is the Resnik threshold, above which an aligned pair is considered correct even if it is not in the TruthFile
ST is a sequence threshold, above which an aligned pair is considered correct even if it is not in the TruthFile
c1 and c2 are the columns in the score file to evaluate (either can be 0 to specify none)
TruthFile is the file of correct matches
G1.el and G2.el are edge lists for the networks; they are not mandatory and can be /dev/null
Scorefile is the file of SANA Core Scores, format is: p1 p2 followed by a list of scores
resnikFile: file used in association with the -R option
seqSim File: file to be used in association with the -S option
complexesFile: CORUM file with list of protein complexes"

die() { echo "$USAGE" >&2; echo "FATAL ERROR: $@" >&2; exit 1;
}

R=1e30
S=1e30
AllOrtho=0
while [ $# -gt 0 ]; do
	case $1 in
	-R) R=$2; shift 2 ;;
	-S) S=$2; shift 2 ;;
	-A) AllOrtho=$2; shift 2;;
	*) break ;;
	esac
done

c1=$1
c2=$2
shift 2
[ $# -ge 4 ] || die "not enough arguments"

if [ $c1 -le 0 ]; then c1=$c2; c2=0; fi # swap them if c1 is not needed
[ $c1 -le 0 ] && die "at least one of c1 and c2 must be > 0"
if [ $c1 -gt 0 -a $c1 -lt 3 ]; then die "cannot evaluate column $c1, that contains protein names"; fi
if [ $c2 -gt 0 -a $c2 -lt 3 ]; then die "cannot evaluate column $c2, that contains protein names"; fi


hawk '
    function EvalScores(c,thresh){TP=TN=FP=FN=0;
	for(p1 in score)for(p2 in score[p1])
	    if(score[p1][p2][c]>thresh){
		if((p1 in O && p2 in O[p1]) || (p1 in R && p2 in R[p1] && R[p1][p2] > 1*'$R') || (p1 in S && p2 in S[p1] && S[p1][p2] > 1*'$S') || 
		    (p2 in complex && index(complex[p2],p1)) || (p1 in complex && index(complex[p1],p2))) ++TP;else ++FP
	    }else{
		if(p1 in O && p2 in O[p1])++FN;else ++TN
	    }
	if(TP+FP==0){Prec=1}else{Prec=TP/(TP+FP)}
	if(TP+FN==0){Rec=1}else{Rec=TP/(TP+FN)}
	if(Prec+Rec==0)F1=0;else F1=2*Prec*Rec/(Prec+Rec)
	TPR=TP/(TP+FN)
	FPR=FP/(FP+TN)
	printf " %6d %7d %6d %7d %.3f %.3f %.3f %.3f %.3f",TP,FP,FN,TN,Prec,Rec,F1,TPR,FPR
	printf " %6d %7d %6d %7d %.3f %.3f %.3f %.3f %.3f",TP,FP,FN,TN,Prec,Rec,F1,TPR,FPR > "'$TMP.AUPR'"
    }

    BEGIN{c1='$c1';c2='$c2'
	if(c1){cols[1]=c1;title[1]="column_"c1;scale[1]=1}
	if(c2){cols[2]=c2;title[2]="column_"c2;scale[2]=1}
	title[3]="Resnik"; scale[3]=14
	title[4]="SeqSim"; scale[4]=100
    }

    ARGIND==1{
	totOrtho++
	O[$1][$2]=1 # Ortholog file, for now, has 2 columns
	# initialize all orthologs in case they are not in scores file?
	if('$AllOrtho') for(c=1;c<=2;c++) score[$1][$2][c] = 0
    }

    # Edge Lists
    ARGIND==2{D1[$1]++;D1[$2]++}
    ARGIND==3{D2[$1]++;D2[$2]++}

    # scores file: p1 p2 [list of 1 or more scores]
    ARGIND==4{
	degreeScore=1;
	if(($1 in D1) && ($2 in D2)&&D1[$1]&&D2[$2]) {
	    #degreeScore = (1-1/MIN(D1[$1],D2[$2]));
	    #degreeScore = (1-1/MAX(D1[$1],D2[$2]));
	    #degreeScore = (1-1/D1[$1])*(1-1/D2[$2]);
	}
	for(c=1;c<=length(cols);c++){
	    score[$1][$2][c]=$cols[c] * degreeScore
	    #if p1,p2 are orthologs, then record their column-c score:
	    if(($1 in O)&&($2 in O[$1]))StatAddSample(c,score[$1][$2][c])
	}
    }

    ARGIND==5&&!/None/&&($2 in score && $3 in score[$2]){ R[$2][$3]=1*$4 } # Resnik score file
    ARGIND==6&&($1 in score && $2 in score[$1]){ S[$1][$2]=1*$3 } # Sequence sims file
    ARGIND==7{for(i=1;i<=NF;i++)complex[$i]=complex[$i]" "$0} # Complexes file
    END{
	delete D1; delete D2; # do not need degrees any more
	printf "%d total orthologs, found %d ortholog pairs in our scores file\n", totOrtho,StatN(1)
	for(c=1;c<=length(cols);c++){
	    printf "cols[%d]=%d %10s mean %8.4f min %8.4f max %8.4f stdDev %8.4f\n",
		c, cols[c], title[c],StatMean(c),StatMin(c),StatMax(c),StatStdDev(c)
	}
	printf "\nEvaluating the scores based on thresholds on core score:\nthresh"
	if(c1)printf "   c_"c1":TP      FP     FN      TN  Prec   Rec   F1   TPR   FPR"
	if(c2)printf  "  c_"c2":TP      FP     FN      TN  Prec   Rec   F1   TPR   FPR"
	printf "\nMeans   "
	for(c=1;c<=length(cols);c++)EvalScores(c,StatMean(c));
	print""
	for(c=1;c<=length(cols);c++){AUPR[c]=0;prevRec[c]=1;prevPrec[c]=0}
	for(thresh=1e-5;thresh<1.01;thresh+=MIN(thresh/2,.05)){
	    printf "%.6f",thresh
	    printf "%.6f",thresh > "'$TMP.AUPR'"
	    done=0;
	    for(c=1;c<=length(cols);c++){
		scaledThresh=scale[c]*thresh;EvalScores(c,scaledThresh) # this sets Prec and Rec
		h=Prec-prevPrec[c]
		AUPR[c]+=h * ((Rec+prevRec[c])/2)
		prevRec[c]=Rec; prevPrec[c]=Prec
		if(Prec==1&&Rec==0)done++;
	    }
	    print""
	    print "" > "'$TMP.AUPR'"
	    if(done == length(cols)) break;
	}
    }' "$@"

# Now compute the AUPR
cols=6
if [ $c1 -ne 0 -a $c2 -ne 0 ]; then cols="$cols 15"; fi
for c in $cols; do
    sort -k ${c}g $TMP.AUPR | hawk 'BEGIN{colName[6]='$c1';colName[15]='$c2';prevRec=1}/^0/{prec=$'$c';rec=$(1+'$c');h=prec-prevPrec;prevPrec=prec;AUPR+=h*((rec+prevRec)/2);prevRec=rec}END{printf "AUPR[%d] = %g\t", colName['$c'],AUPR}'
done
echo "" # Final newline after both AUPRs are printed
