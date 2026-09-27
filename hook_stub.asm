; Assembled by ml64 into the plugin loader. Each stub is entered from a thunk
; allocated near the game module, which jumps to it with every register intact;
; rsp is the game's rsp at the hook site.
;
; The push order is the SavedRegisters layout the callback reads, and the order
; of the LOADER_REG_ numbers in tlou_plugin_sdk.h: rax at [rsp+0], r15 at
; [rsp+70h]. The three must match.
;
; Two hundred and fifty six stubs, hookStub00 to hookStub255, each with its own
; callback pointer and return address. Names below a hundred have two digits.
; The loader's HOOK_STUB_LIST must name every one.
;
; A stub's return address must point at a buffer holding the bytes the hook
; displaced, followed by an absolute jump back past the patch.

.DATA

PUBLIC global_hookCallback00
PUBLIC global_returnAddress00
PUBLIC global_hookCallback01
PUBLIC global_returnAddress01
PUBLIC global_hookCallback02
PUBLIC global_returnAddress02
PUBLIC global_hookCallback03
PUBLIC global_returnAddress03
PUBLIC global_hookCallback04
PUBLIC global_returnAddress04
PUBLIC global_hookCallback05
PUBLIC global_returnAddress05
PUBLIC global_hookCallback06
PUBLIC global_returnAddress06
PUBLIC global_hookCallback07
PUBLIC global_returnAddress07
PUBLIC global_hookCallback08
PUBLIC global_returnAddress08
PUBLIC global_hookCallback09
PUBLIC global_returnAddress09
PUBLIC global_hookCallback10
PUBLIC global_returnAddress10
PUBLIC global_hookCallback11
PUBLIC global_returnAddress11
PUBLIC global_hookCallback12
PUBLIC global_returnAddress12
PUBLIC global_hookCallback13
PUBLIC global_returnAddress13
PUBLIC global_hookCallback14
PUBLIC global_returnAddress14
PUBLIC global_hookCallback15
PUBLIC global_returnAddress15
PUBLIC global_hookCallback16
PUBLIC global_returnAddress16
PUBLIC global_hookCallback17
PUBLIC global_returnAddress17
PUBLIC global_hookCallback18
PUBLIC global_returnAddress18
PUBLIC global_hookCallback19
PUBLIC global_returnAddress19
PUBLIC global_hookCallback20
PUBLIC global_returnAddress20
PUBLIC global_hookCallback21
PUBLIC global_returnAddress21
PUBLIC global_hookCallback22
PUBLIC global_returnAddress22
PUBLIC global_hookCallback23
PUBLIC global_returnAddress23
PUBLIC global_hookCallback24
PUBLIC global_returnAddress24
PUBLIC global_hookCallback25
PUBLIC global_returnAddress25
PUBLIC global_hookCallback26
PUBLIC global_returnAddress26
PUBLIC global_hookCallback27
PUBLIC global_returnAddress27
PUBLIC global_hookCallback28
PUBLIC global_returnAddress28
PUBLIC global_hookCallback29
PUBLIC global_returnAddress29
PUBLIC global_hookCallback30
PUBLIC global_returnAddress30
PUBLIC global_hookCallback31
PUBLIC global_returnAddress31
PUBLIC global_hookCallback32
PUBLIC global_returnAddress32
PUBLIC global_hookCallback33
PUBLIC global_returnAddress33
PUBLIC global_hookCallback34
PUBLIC global_returnAddress34
PUBLIC global_hookCallback35
PUBLIC global_returnAddress35
PUBLIC global_hookCallback36
PUBLIC global_returnAddress36
PUBLIC global_hookCallback37
PUBLIC global_returnAddress37
PUBLIC global_hookCallback38
PUBLIC global_returnAddress38
PUBLIC global_hookCallback39
PUBLIC global_returnAddress39
PUBLIC global_hookCallback40
PUBLIC global_returnAddress40
PUBLIC global_hookCallback41
PUBLIC global_returnAddress41
PUBLIC global_hookCallback42
PUBLIC global_returnAddress42
PUBLIC global_hookCallback43
PUBLIC global_returnAddress43
PUBLIC global_hookCallback44
PUBLIC global_returnAddress44
PUBLIC global_hookCallback45
PUBLIC global_returnAddress45
PUBLIC global_hookCallback46
PUBLIC global_returnAddress46
PUBLIC global_hookCallback47
PUBLIC global_returnAddress47
PUBLIC global_hookCallback48
PUBLIC global_returnAddress48
PUBLIC global_hookCallback49
PUBLIC global_returnAddress49
PUBLIC global_hookCallback50
PUBLIC global_returnAddress50
PUBLIC global_hookCallback51
PUBLIC global_returnAddress51
PUBLIC global_hookCallback52
PUBLIC global_returnAddress52
PUBLIC global_hookCallback53
PUBLIC global_returnAddress53
PUBLIC global_hookCallback54
PUBLIC global_returnAddress54
PUBLIC global_hookCallback55
PUBLIC global_returnAddress55
PUBLIC global_hookCallback56
PUBLIC global_returnAddress56
PUBLIC global_hookCallback57
PUBLIC global_returnAddress57
PUBLIC global_hookCallback58
PUBLIC global_returnAddress58
PUBLIC global_hookCallback59
PUBLIC global_returnAddress59
PUBLIC global_hookCallback60
PUBLIC global_returnAddress60
PUBLIC global_hookCallback61
PUBLIC global_returnAddress61
PUBLIC global_hookCallback62
PUBLIC global_returnAddress62
PUBLIC global_hookCallback63
PUBLIC global_returnAddress63
PUBLIC global_hookCallback64
PUBLIC global_returnAddress64
PUBLIC global_hookCallback65
PUBLIC global_returnAddress65
PUBLIC global_hookCallback66
PUBLIC global_returnAddress66
PUBLIC global_hookCallback67
PUBLIC global_returnAddress67
PUBLIC global_hookCallback68
PUBLIC global_returnAddress68
PUBLIC global_hookCallback69
PUBLIC global_returnAddress69
PUBLIC global_hookCallback70
PUBLIC global_returnAddress70
PUBLIC global_hookCallback71
PUBLIC global_returnAddress71
PUBLIC global_hookCallback72
PUBLIC global_returnAddress72
PUBLIC global_hookCallback73
PUBLIC global_returnAddress73
PUBLIC global_hookCallback74
PUBLIC global_returnAddress74
PUBLIC global_hookCallback75
PUBLIC global_returnAddress75
PUBLIC global_hookCallback76
PUBLIC global_returnAddress76
PUBLIC global_hookCallback77
PUBLIC global_returnAddress77
PUBLIC global_hookCallback78
PUBLIC global_returnAddress78
PUBLIC global_hookCallback79
PUBLIC global_returnAddress79
PUBLIC global_hookCallback80
PUBLIC global_returnAddress80
PUBLIC global_hookCallback81
PUBLIC global_returnAddress81
PUBLIC global_hookCallback82
PUBLIC global_returnAddress82
PUBLIC global_hookCallback83
PUBLIC global_returnAddress83
PUBLIC global_hookCallback84
PUBLIC global_returnAddress84
PUBLIC global_hookCallback85
PUBLIC global_returnAddress85
PUBLIC global_hookCallback86
PUBLIC global_returnAddress86
PUBLIC global_hookCallback87
PUBLIC global_returnAddress87
PUBLIC global_hookCallback88
PUBLIC global_returnAddress88
PUBLIC global_hookCallback89
PUBLIC global_returnAddress89
PUBLIC global_hookCallback90
PUBLIC global_returnAddress90
PUBLIC global_hookCallback91
PUBLIC global_returnAddress91
PUBLIC global_hookCallback92
PUBLIC global_returnAddress92
PUBLIC global_hookCallback93
PUBLIC global_returnAddress93
PUBLIC global_hookCallback94
PUBLIC global_returnAddress94
PUBLIC global_hookCallback95
PUBLIC global_returnAddress95
PUBLIC global_hookCallback96
PUBLIC global_returnAddress96
PUBLIC global_hookCallback97
PUBLIC global_returnAddress97
PUBLIC global_hookCallback98
PUBLIC global_returnAddress98
PUBLIC global_hookCallback99
PUBLIC global_returnAddress99
PUBLIC global_hookCallback100
PUBLIC global_returnAddress100
PUBLIC global_hookCallback101
PUBLIC global_returnAddress101
PUBLIC global_hookCallback102
PUBLIC global_returnAddress102
PUBLIC global_hookCallback103
PUBLIC global_returnAddress103
PUBLIC global_hookCallback104
PUBLIC global_returnAddress104
PUBLIC global_hookCallback105
PUBLIC global_returnAddress105
PUBLIC global_hookCallback106
PUBLIC global_returnAddress106
PUBLIC global_hookCallback107
PUBLIC global_returnAddress107
PUBLIC global_hookCallback108
PUBLIC global_returnAddress108
PUBLIC global_hookCallback109
PUBLIC global_returnAddress109
PUBLIC global_hookCallback110
PUBLIC global_returnAddress110
PUBLIC global_hookCallback111
PUBLIC global_returnAddress111
PUBLIC global_hookCallback112
PUBLIC global_returnAddress112
PUBLIC global_hookCallback113
PUBLIC global_returnAddress113
PUBLIC global_hookCallback114
PUBLIC global_returnAddress114
PUBLIC global_hookCallback115
PUBLIC global_returnAddress115
PUBLIC global_hookCallback116
PUBLIC global_returnAddress116
PUBLIC global_hookCallback117
PUBLIC global_returnAddress117
PUBLIC global_hookCallback118
PUBLIC global_returnAddress118
PUBLIC global_hookCallback119
PUBLIC global_returnAddress119
PUBLIC global_hookCallback120
PUBLIC global_returnAddress120
PUBLIC global_hookCallback121
PUBLIC global_returnAddress121
PUBLIC global_hookCallback122
PUBLIC global_returnAddress122
PUBLIC global_hookCallback123
PUBLIC global_returnAddress123
PUBLIC global_hookCallback124
PUBLIC global_returnAddress124
PUBLIC global_hookCallback125
PUBLIC global_returnAddress125
PUBLIC global_hookCallback126
PUBLIC global_returnAddress126
PUBLIC global_hookCallback127
PUBLIC global_returnAddress127
PUBLIC global_hookCallback128
PUBLIC global_returnAddress128
PUBLIC global_hookCallback129
PUBLIC global_returnAddress129
PUBLIC global_hookCallback130
PUBLIC global_returnAddress130
PUBLIC global_hookCallback131
PUBLIC global_returnAddress131
PUBLIC global_hookCallback132
PUBLIC global_returnAddress132
PUBLIC global_hookCallback133
PUBLIC global_returnAddress133
PUBLIC global_hookCallback134
PUBLIC global_returnAddress134
PUBLIC global_hookCallback135
PUBLIC global_returnAddress135
PUBLIC global_hookCallback136
PUBLIC global_returnAddress136
PUBLIC global_hookCallback137
PUBLIC global_returnAddress137
PUBLIC global_hookCallback138
PUBLIC global_returnAddress138
PUBLIC global_hookCallback139
PUBLIC global_returnAddress139
PUBLIC global_hookCallback140
PUBLIC global_returnAddress140
PUBLIC global_hookCallback141
PUBLIC global_returnAddress141
PUBLIC global_hookCallback142
PUBLIC global_returnAddress142
PUBLIC global_hookCallback143
PUBLIC global_returnAddress143
PUBLIC global_hookCallback144
PUBLIC global_returnAddress144
PUBLIC global_hookCallback145
PUBLIC global_returnAddress145
PUBLIC global_hookCallback146
PUBLIC global_returnAddress146
PUBLIC global_hookCallback147
PUBLIC global_returnAddress147
PUBLIC global_hookCallback148
PUBLIC global_returnAddress148
PUBLIC global_hookCallback149
PUBLIC global_returnAddress149
PUBLIC global_hookCallback150
PUBLIC global_returnAddress150
PUBLIC global_hookCallback151
PUBLIC global_returnAddress151
PUBLIC global_hookCallback152
PUBLIC global_returnAddress152
PUBLIC global_hookCallback153
PUBLIC global_returnAddress153
PUBLIC global_hookCallback154
PUBLIC global_returnAddress154
PUBLIC global_hookCallback155
PUBLIC global_returnAddress155
PUBLIC global_hookCallback156
PUBLIC global_returnAddress156
PUBLIC global_hookCallback157
PUBLIC global_returnAddress157
PUBLIC global_hookCallback158
PUBLIC global_returnAddress158
PUBLIC global_hookCallback159
PUBLIC global_returnAddress159
PUBLIC global_hookCallback160
PUBLIC global_returnAddress160
PUBLIC global_hookCallback161
PUBLIC global_returnAddress161
PUBLIC global_hookCallback162
PUBLIC global_returnAddress162
PUBLIC global_hookCallback163
PUBLIC global_returnAddress163
PUBLIC global_hookCallback164
PUBLIC global_returnAddress164
PUBLIC global_hookCallback165
PUBLIC global_returnAddress165
PUBLIC global_hookCallback166
PUBLIC global_returnAddress166
PUBLIC global_hookCallback167
PUBLIC global_returnAddress167
PUBLIC global_hookCallback168
PUBLIC global_returnAddress168
PUBLIC global_hookCallback169
PUBLIC global_returnAddress169
PUBLIC global_hookCallback170
PUBLIC global_returnAddress170
PUBLIC global_hookCallback171
PUBLIC global_returnAddress171
PUBLIC global_hookCallback172
PUBLIC global_returnAddress172
PUBLIC global_hookCallback173
PUBLIC global_returnAddress173
PUBLIC global_hookCallback174
PUBLIC global_returnAddress174
PUBLIC global_hookCallback175
PUBLIC global_returnAddress175
PUBLIC global_hookCallback176
PUBLIC global_returnAddress176
PUBLIC global_hookCallback177
PUBLIC global_returnAddress177
PUBLIC global_hookCallback178
PUBLIC global_returnAddress178
PUBLIC global_hookCallback179
PUBLIC global_returnAddress179
PUBLIC global_hookCallback180
PUBLIC global_returnAddress180
PUBLIC global_hookCallback181
PUBLIC global_returnAddress181
PUBLIC global_hookCallback182
PUBLIC global_returnAddress182
PUBLIC global_hookCallback183
PUBLIC global_returnAddress183
PUBLIC global_hookCallback184
PUBLIC global_returnAddress184
PUBLIC global_hookCallback185
PUBLIC global_returnAddress185
PUBLIC global_hookCallback186
PUBLIC global_returnAddress186
PUBLIC global_hookCallback187
PUBLIC global_returnAddress187
PUBLIC global_hookCallback188
PUBLIC global_returnAddress188
PUBLIC global_hookCallback189
PUBLIC global_returnAddress189
PUBLIC global_hookCallback190
PUBLIC global_returnAddress190
PUBLIC global_hookCallback191
PUBLIC global_returnAddress191
PUBLIC global_hookCallback192
PUBLIC global_returnAddress192
PUBLIC global_hookCallback193
PUBLIC global_returnAddress193
PUBLIC global_hookCallback194
PUBLIC global_returnAddress194
PUBLIC global_hookCallback195
PUBLIC global_returnAddress195
PUBLIC global_hookCallback196
PUBLIC global_returnAddress196
PUBLIC global_hookCallback197
PUBLIC global_returnAddress197
PUBLIC global_hookCallback198
PUBLIC global_returnAddress198
PUBLIC global_hookCallback199
PUBLIC global_returnAddress199
PUBLIC global_hookCallback200
PUBLIC global_returnAddress200
PUBLIC global_hookCallback201
PUBLIC global_returnAddress201
PUBLIC global_hookCallback202
PUBLIC global_returnAddress202
PUBLIC global_hookCallback203
PUBLIC global_returnAddress203
PUBLIC global_hookCallback204
PUBLIC global_returnAddress204
PUBLIC global_hookCallback205
PUBLIC global_returnAddress205
PUBLIC global_hookCallback206
PUBLIC global_returnAddress206
PUBLIC global_hookCallback207
PUBLIC global_returnAddress207
PUBLIC global_hookCallback208
PUBLIC global_returnAddress208
PUBLIC global_hookCallback209
PUBLIC global_returnAddress209
PUBLIC global_hookCallback210
PUBLIC global_returnAddress210
PUBLIC global_hookCallback211
PUBLIC global_returnAddress211
PUBLIC global_hookCallback212
PUBLIC global_returnAddress212
PUBLIC global_hookCallback213
PUBLIC global_returnAddress213
PUBLIC global_hookCallback214
PUBLIC global_returnAddress214
PUBLIC global_hookCallback215
PUBLIC global_returnAddress215
PUBLIC global_hookCallback216
PUBLIC global_returnAddress216
PUBLIC global_hookCallback217
PUBLIC global_returnAddress217
PUBLIC global_hookCallback218
PUBLIC global_returnAddress218
PUBLIC global_hookCallback219
PUBLIC global_returnAddress219
PUBLIC global_hookCallback220
PUBLIC global_returnAddress220
PUBLIC global_hookCallback221
PUBLIC global_returnAddress221
PUBLIC global_hookCallback222
PUBLIC global_returnAddress222
PUBLIC global_hookCallback223
PUBLIC global_returnAddress223
PUBLIC global_hookCallback224
PUBLIC global_returnAddress224
PUBLIC global_hookCallback225
PUBLIC global_returnAddress225
PUBLIC global_hookCallback226
PUBLIC global_returnAddress226
PUBLIC global_hookCallback227
PUBLIC global_returnAddress227
PUBLIC global_hookCallback228
PUBLIC global_returnAddress228
PUBLIC global_hookCallback229
PUBLIC global_returnAddress229
PUBLIC global_hookCallback230
PUBLIC global_returnAddress230
PUBLIC global_hookCallback231
PUBLIC global_returnAddress231
PUBLIC global_hookCallback232
PUBLIC global_returnAddress232
PUBLIC global_hookCallback233
PUBLIC global_returnAddress233
PUBLIC global_hookCallback234
PUBLIC global_returnAddress234
PUBLIC global_hookCallback235
PUBLIC global_returnAddress235
PUBLIC global_hookCallback236
PUBLIC global_returnAddress236
PUBLIC global_hookCallback237
PUBLIC global_returnAddress237
PUBLIC global_hookCallback238
PUBLIC global_returnAddress238
PUBLIC global_hookCallback239
PUBLIC global_returnAddress239
PUBLIC global_hookCallback240
PUBLIC global_returnAddress240
PUBLIC global_hookCallback241
PUBLIC global_returnAddress241
PUBLIC global_hookCallback242
PUBLIC global_returnAddress242
PUBLIC global_hookCallback243
PUBLIC global_returnAddress243
PUBLIC global_hookCallback244
PUBLIC global_returnAddress244
PUBLIC global_hookCallback245
PUBLIC global_returnAddress245
PUBLIC global_hookCallback246
PUBLIC global_returnAddress246
PUBLIC global_hookCallback247
PUBLIC global_returnAddress247
PUBLIC global_hookCallback248
PUBLIC global_returnAddress248
PUBLIC global_hookCallback249
PUBLIC global_returnAddress249
PUBLIC global_hookCallback250
PUBLIC global_returnAddress250
PUBLIC global_hookCallback251
PUBLIC global_returnAddress251
PUBLIC global_hookCallback252
PUBLIC global_returnAddress252
PUBLIC global_hookCallback253
PUBLIC global_returnAddress253
PUBLIC global_hookCallback254
PUBLIC global_returnAddress254
PUBLIC global_hookCallback255
PUBLIC global_returnAddress255

global_hookCallback00     QWORD 0
global_returnAddress00    QWORD 0
global_hookCallback01     QWORD 0
global_returnAddress01    QWORD 0
global_hookCallback02     QWORD 0
global_returnAddress02    QWORD 0
global_hookCallback03     QWORD 0
global_returnAddress03    QWORD 0
global_hookCallback04     QWORD 0
global_returnAddress04    QWORD 0
global_hookCallback05     QWORD 0
global_returnAddress05    QWORD 0
global_hookCallback06     QWORD 0
global_returnAddress06    QWORD 0
global_hookCallback07     QWORD 0
global_returnAddress07    QWORD 0
global_hookCallback08     QWORD 0
global_returnAddress08    QWORD 0
global_hookCallback09     QWORD 0
global_returnAddress09    QWORD 0
global_hookCallback10     QWORD 0
global_returnAddress10    QWORD 0
global_hookCallback11     QWORD 0
global_returnAddress11    QWORD 0
global_hookCallback12     QWORD 0
global_returnAddress12    QWORD 0
global_hookCallback13     QWORD 0
global_returnAddress13    QWORD 0
global_hookCallback14     QWORD 0
global_returnAddress14    QWORD 0
global_hookCallback15     QWORD 0
global_returnAddress15    QWORD 0
global_hookCallback16     QWORD 0
global_returnAddress16    QWORD 0
global_hookCallback17     QWORD 0
global_returnAddress17    QWORD 0
global_hookCallback18     QWORD 0
global_returnAddress18    QWORD 0
global_hookCallback19     QWORD 0
global_returnAddress19    QWORD 0
global_hookCallback20     QWORD 0
global_returnAddress20    QWORD 0
global_hookCallback21     QWORD 0
global_returnAddress21    QWORD 0
global_hookCallback22     QWORD 0
global_returnAddress22    QWORD 0
global_hookCallback23     QWORD 0
global_returnAddress23    QWORD 0
global_hookCallback24     QWORD 0
global_returnAddress24    QWORD 0
global_hookCallback25     QWORD 0
global_returnAddress25    QWORD 0
global_hookCallback26     QWORD 0
global_returnAddress26    QWORD 0
global_hookCallback27     QWORD 0
global_returnAddress27    QWORD 0
global_hookCallback28     QWORD 0
global_returnAddress28    QWORD 0
global_hookCallback29     QWORD 0
global_returnAddress29    QWORD 0
global_hookCallback30     QWORD 0
global_returnAddress30    QWORD 0
global_hookCallback31     QWORD 0
global_returnAddress31    QWORD 0
global_hookCallback32     QWORD 0
global_returnAddress32    QWORD 0
global_hookCallback33     QWORD 0
global_returnAddress33    QWORD 0
global_hookCallback34     QWORD 0
global_returnAddress34    QWORD 0
global_hookCallback35     QWORD 0
global_returnAddress35    QWORD 0
global_hookCallback36     QWORD 0
global_returnAddress36    QWORD 0
global_hookCallback37     QWORD 0
global_returnAddress37    QWORD 0
global_hookCallback38     QWORD 0
global_returnAddress38    QWORD 0
global_hookCallback39     QWORD 0
global_returnAddress39    QWORD 0
global_hookCallback40     QWORD 0
global_returnAddress40    QWORD 0
global_hookCallback41     QWORD 0
global_returnAddress41    QWORD 0
global_hookCallback42     QWORD 0
global_returnAddress42    QWORD 0
global_hookCallback43     QWORD 0
global_returnAddress43    QWORD 0
global_hookCallback44     QWORD 0
global_returnAddress44    QWORD 0
global_hookCallback45     QWORD 0
global_returnAddress45    QWORD 0
global_hookCallback46     QWORD 0
global_returnAddress46    QWORD 0
global_hookCallback47     QWORD 0
global_returnAddress47    QWORD 0
global_hookCallback48     QWORD 0
global_returnAddress48    QWORD 0
global_hookCallback49     QWORD 0
global_returnAddress49    QWORD 0
global_hookCallback50     QWORD 0
global_returnAddress50    QWORD 0
global_hookCallback51     QWORD 0
global_returnAddress51    QWORD 0
global_hookCallback52     QWORD 0
global_returnAddress52    QWORD 0
global_hookCallback53     QWORD 0
global_returnAddress53    QWORD 0
global_hookCallback54     QWORD 0
global_returnAddress54    QWORD 0
global_hookCallback55     QWORD 0
global_returnAddress55    QWORD 0
global_hookCallback56     QWORD 0
global_returnAddress56    QWORD 0
global_hookCallback57     QWORD 0
global_returnAddress57    QWORD 0
global_hookCallback58     QWORD 0
global_returnAddress58    QWORD 0
global_hookCallback59     QWORD 0
global_returnAddress59    QWORD 0
global_hookCallback60     QWORD 0
global_returnAddress60    QWORD 0
global_hookCallback61     QWORD 0
global_returnAddress61    QWORD 0
global_hookCallback62     QWORD 0
global_returnAddress62    QWORD 0
global_hookCallback63     QWORD 0
global_returnAddress63    QWORD 0
global_hookCallback64     QWORD 0
global_returnAddress64    QWORD 0
global_hookCallback65     QWORD 0
global_returnAddress65    QWORD 0
global_hookCallback66     QWORD 0
global_returnAddress66    QWORD 0
global_hookCallback67     QWORD 0
global_returnAddress67    QWORD 0
global_hookCallback68     QWORD 0
global_returnAddress68    QWORD 0
global_hookCallback69     QWORD 0
global_returnAddress69    QWORD 0
global_hookCallback70     QWORD 0
global_returnAddress70    QWORD 0
global_hookCallback71     QWORD 0
global_returnAddress71    QWORD 0
global_hookCallback72     QWORD 0
global_returnAddress72    QWORD 0
global_hookCallback73     QWORD 0
global_returnAddress73    QWORD 0
global_hookCallback74     QWORD 0
global_returnAddress74    QWORD 0
global_hookCallback75     QWORD 0
global_returnAddress75    QWORD 0
global_hookCallback76     QWORD 0
global_returnAddress76    QWORD 0
global_hookCallback77     QWORD 0
global_returnAddress77    QWORD 0
global_hookCallback78     QWORD 0
global_returnAddress78    QWORD 0
global_hookCallback79     QWORD 0
global_returnAddress79    QWORD 0
global_hookCallback80     QWORD 0
global_returnAddress80    QWORD 0
global_hookCallback81     QWORD 0
global_returnAddress81    QWORD 0
global_hookCallback82     QWORD 0
global_returnAddress82    QWORD 0
global_hookCallback83     QWORD 0
global_returnAddress83    QWORD 0
global_hookCallback84     QWORD 0
global_returnAddress84    QWORD 0
global_hookCallback85     QWORD 0
global_returnAddress85    QWORD 0
global_hookCallback86     QWORD 0
global_returnAddress86    QWORD 0
global_hookCallback87     QWORD 0
global_returnAddress87    QWORD 0
global_hookCallback88     QWORD 0
global_returnAddress88    QWORD 0
global_hookCallback89     QWORD 0
global_returnAddress89    QWORD 0
global_hookCallback90     QWORD 0
global_returnAddress90    QWORD 0
global_hookCallback91     QWORD 0
global_returnAddress91    QWORD 0
global_hookCallback92     QWORD 0
global_returnAddress92    QWORD 0
global_hookCallback93     QWORD 0
global_returnAddress93    QWORD 0
global_hookCallback94     QWORD 0
global_returnAddress94    QWORD 0
global_hookCallback95     QWORD 0
global_returnAddress95    QWORD 0
global_hookCallback96     QWORD 0
global_returnAddress96    QWORD 0
global_hookCallback97     QWORD 0
global_returnAddress97    QWORD 0
global_hookCallback98     QWORD 0
global_returnAddress98    QWORD 0
global_hookCallback99     QWORD 0
global_returnAddress99    QWORD 0
global_hookCallback100    QWORD 0
global_returnAddress100   QWORD 0
global_hookCallback101    QWORD 0
global_returnAddress101   QWORD 0
global_hookCallback102    QWORD 0
global_returnAddress102   QWORD 0
global_hookCallback103    QWORD 0
global_returnAddress103   QWORD 0
global_hookCallback104    QWORD 0
global_returnAddress104   QWORD 0
global_hookCallback105    QWORD 0
global_returnAddress105   QWORD 0
global_hookCallback106    QWORD 0
global_returnAddress106   QWORD 0
global_hookCallback107    QWORD 0
global_returnAddress107   QWORD 0
global_hookCallback108    QWORD 0
global_returnAddress108   QWORD 0
global_hookCallback109    QWORD 0
global_returnAddress109   QWORD 0
global_hookCallback110    QWORD 0
global_returnAddress110   QWORD 0
global_hookCallback111    QWORD 0
global_returnAddress111   QWORD 0
global_hookCallback112    QWORD 0
global_returnAddress112   QWORD 0
global_hookCallback113    QWORD 0
global_returnAddress113   QWORD 0
global_hookCallback114    QWORD 0
global_returnAddress114   QWORD 0
global_hookCallback115    QWORD 0
global_returnAddress115   QWORD 0
global_hookCallback116    QWORD 0
global_returnAddress116   QWORD 0
global_hookCallback117    QWORD 0
global_returnAddress117   QWORD 0
global_hookCallback118    QWORD 0
global_returnAddress118   QWORD 0
global_hookCallback119    QWORD 0
global_returnAddress119   QWORD 0
global_hookCallback120    QWORD 0
global_returnAddress120   QWORD 0
global_hookCallback121    QWORD 0
global_returnAddress121   QWORD 0
global_hookCallback122    QWORD 0
global_returnAddress122   QWORD 0
global_hookCallback123    QWORD 0
global_returnAddress123   QWORD 0
global_hookCallback124    QWORD 0
global_returnAddress124   QWORD 0
global_hookCallback125    QWORD 0
global_returnAddress125   QWORD 0
global_hookCallback126    QWORD 0
global_returnAddress126   QWORD 0
global_hookCallback127    QWORD 0
global_returnAddress127   QWORD 0
global_hookCallback128    QWORD 0
global_returnAddress128   QWORD 0
global_hookCallback129    QWORD 0
global_returnAddress129   QWORD 0
global_hookCallback130    QWORD 0
global_returnAddress130   QWORD 0
global_hookCallback131    QWORD 0
global_returnAddress131   QWORD 0
global_hookCallback132    QWORD 0
global_returnAddress132   QWORD 0
global_hookCallback133    QWORD 0
global_returnAddress133   QWORD 0
global_hookCallback134    QWORD 0
global_returnAddress134   QWORD 0
global_hookCallback135    QWORD 0
global_returnAddress135   QWORD 0
global_hookCallback136    QWORD 0
global_returnAddress136   QWORD 0
global_hookCallback137    QWORD 0
global_returnAddress137   QWORD 0
global_hookCallback138    QWORD 0
global_returnAddress138   QWORD 0
global_hookCallback139    QWORD 0
global_returnAddress139   QWORD 0
global_hookCallback140    QWORD 0
global_returnAddress140   QWORD 0
global_hookCallback141    QWORD 0
global_returnAddress141   QWORD 0
global_hookCallback142    QWORD 0
global_returnAddress142   QWORD 0
global_hookCallback143    QWORD 0
global_returnAddress143   QWORD 0
global_hookCallback144    QWORD 0
global_returnAddress144   QWORD 0
global_hookCallback145    QWORD 0
global_returnAddress145   QWORD 0
global_hookCallback146    QWORD 0
global_returnAddress146   QWORD 0
global_hookCallback147    QWORD 0
global_returnAddress147   QWORD 0
global_hookCallback148    QWORD 0
global_returnAddress148   QWORD 0
global_hookCallback149    QWORD 0
global_returnAddress149   QWORD 0
global_hookCallback150    QWORD 0
global_returnAddress150   QWORD 0
global_hookCallback151    QWORD 0
global_returnAddress151   QWORD 0
global_hookCallback152    QWORD 0
global_returnAddress152   QWORD 0
global_hookCallback153    QWORD 0
global_returnAddress153   QWORD 0
global_hookCallback154    QWORD 0
global_returnAddress154   QWORD 0
global_hookCallback155    QWORD 0
global_returnAddress155   QWORD 0
global_hookCallback156    QWORD 0
global_returnAddress156   QWORD 0
global_hookCallback157    QWORD 0
global_returnAddress157   QWORD 0
global_hookCallback158    QWORD 0
global_returnAddress158   QWORD 0
global_hookCallback159    QWORD 0
global_returnAddress159   QWORD 0
global_hookCallback160    QWORD 0
global_returnAddress160   QWORD 0
global_hookCallback161    QWORD 0
global_returnAddress161   QWORD 0
global_hookCallback162    QWORD 0
global_returnAddress162   QWORD 0
global_hookCallback163    QWORD 0
global_returnAddress163   QWORD 0
global_hookCallback164    QWORD 0
global_returnAddress164   QWORD 0
global_hookCallback165    QWORD 0
global_returnAddress165   QWORD 0
global_hookCallback166    QWORD 0
global_returnAddress166   QWORD 0
global_hookCallback167    QWORD 0
global_returnAddress167   QWORD 0
global_hookCallback168    QWORD 0
global_returnAddress168   QWORD 0
global_hookCallback169    QWORD 0
global_returnAddress169   QWORD 0
global_hookCallback170    QWORD 0
global_returnAddress170   QWORD 0
global_hookCallback171    QWORD 0
global_returnAddress171   QWORD 0
global_hookCallback172    QWORD 0
global_returnAddress172   QWORD 0
global_hookCallback173    QWORD 0
global_returnAddress173   QWORD 0
global_hookCallback174    QWORD 0
global_returnAddress174   QWORD 0
global_hookCallback175    QWORD 0
global_returnAddress175   QWORD 0
global_hookCallback176    QWORD 0
global_returnAddress176   QWORD 0
global_hookCallback177    QWORD 0
global_returnAddress177   QWORD 0
global_hookCallback178    QWORD 0
global_returnAddress178   QWORD 0
global_hookCallback179    QWORD 0
global_returnAddress179   QWORD 0
global_hookCallback180    QWORD 0
global_returnAddress180   QWORD 0
global_hookCallback181    QWORD 0
global_returnAddress181   QWORD 0
global_hookCallback182    QWORD 0
global_returnAddress182   QWORD 0
global_hookCallback183    QWORD 0
global_returnAddress183   QWORD 0
global_hookCallback184    QWORD 0
global_returnAddress184   QWORD 0
global_hookCallback185    QWORD 0
global_returnAddress185   QWORD 0
global_hookCallback186    QWORD 0
global_returnAddress186   QWORD 0
global_hookCallback187    QWORD 0
global_returnAddress187   QWORD 0
global_hookCallback188    QWORD 0
global_returnAddress188   QWORD 0
global_hookCallback189    QWORD 0
global_returnAddress189   QWORD 0
global_hookCallback190    QWORD 0
global_returnAddress190   QWORD 0
global_hookCallback191    QWORD 0
global_returnAddress191   QWORD 0
global_hookCallback192    QWORD 0
global_returnAddress192   QWORD 0
global_hookCallback193    QWORD 0
global_returnAddress193   QWORD 0
global_hookCallback194    QWORD 0
global_returnAddress194   QWORD 0
global_hookCallback195    QWORD 0
global_returnAddress195   QWORD 0
global_hookCallback196    QWORD 0
global_returnAddress196   QWORD 0
global_hookCallback197    QWORD 0
global_returnAddress197   QWORD 0
global_hookCallback198    QWORD 0
global_returnAddress198   QWORD 0
global_hookCallback199    QWORD 0
global_returnAddress199   QWORD 0
global_hookCallback200    QWORD 0
global_returnAddress200   QWORD 0
global_hookCallback201    QWORD 0
global_returnAddress201   QWORD 0
global_hookCallback202    QWORD 0
global_returnAddress202   QWORD 0
global_hookCallback203    QWORD 0
global_returnAddress203   QWORD 0
global_hookCallback204    QWORD 0
global_returnAddress204   QWORD 0
global_hookCallback205    QWORD 0
global_returnAddress205   QWORD 0
global_hookCallback206    QWORD 0
global_returnAddress206   QWORD 0
global_hookCallback207    QWORD 0
global_returnAddress207   QWORD 0
global_hookCallback208    QWORD 0
global_returnAddress208   QWORD 0
global_hookCallback209    QWORD 0
global_returnAddress209   QWORD 0
global_hookCallback210    QWORD 0
global_returnAddress210   QWORD 0
global_hookCallback211    QWORD 0
global_returnAddress211   QWORD 0
global_hookCallback212    QWORD 0
global_returnAddress212   QWORD 0
global_hookCallback213    QWORD 0
global_returnAddress213   QWORD 0
global_hookCallback214    QWORD 0
global_returnAddress214   QWORD 0
global_hookCallback215    QWORD 0
global_returnAddress215   QWORD 0
global_hookCallback216    QWORD 0
global_returnAddress216   QWORD 0
global_hookCallback217    QWORD 0
global_returnAddress217   QWORD 0
global_hookCallback218    QWORD 0
global_returnAddress218   QWORD 0
global_hookCallback219    QWORD 0
global_returnAddress219   QWORD 0
global_hookCallback220    QWORD 0
global_returnAddress220   QWORD 0
global_hookCallback221    QWORD 0
global_returnAddress221   QWORD 0
global_hookCallback222    QWORD 0
global_returnAddress222   QWORD 0
global_hookCallback223    QWORD 0
global_returnAddress223   QWORD 0
global_hookCallback224    QWORD 0
global_returnAddress224   QWORD 0
global_hookCallback225    QWORD 0
global_returnAddress225   QWORD 0
global_hookCallback226    QWORD 0
global_returnAddress226   QWORD 0
global_hookCallback227    QWORD 0
global_returnAddress227   QWORD 0
global_hookCallback228    QWORD 0
global_returnAddress228   QWORD 0
global_hookCallback229    QWORD 0
global_returnAddress229   QWORD 0
global_hookCallback230    QWORD 0
global_returnAddress230   QWORD 0
global_hookCallback231    QWORD 0
global_returnAddress231   QWORD 0
global_hookCallback232    QWORD 0
global_returnAddress232   QWORD 0
global_hookCallback233    QWORD 0
global_returnAddress233   QWORD 0
global_hookCallback234    QWORD 0
global_returnAddress234   QWORD 0
global_hookCallback235    QWORD 0
global_returnAddress235   QWORD 0
global_hookCallback236    QWORD 0
global_returnAddress236   QWORD 0
global_hookCallback237    QWORD 0
global_returnAddress237   QWORD 0
global_hookCallback238    QWORD 0
global_returnAddress238   QWORD 0
global_hookCallback239    QWORD 0
global_returnAddress239   QWORD 0
global_hookCallback240    QWORD 0
global_returnAddress240   QWORD 0
global_hookCallback241    QWORD 0
global_returnAddress241   QWORD 0
global_hookCallback242    QWORD 0
global_returnAddress242   QWORD 0
global_hookCallback243    QWORD 0
global_returnAddress243   QWORD 0
global_hookCallback244    QWORD 0
global_returnAddress244   QWORD 0
global_hookCallback245    QWORD 0
global_returnAddress245   QWORD 0
global_hookCallback246    QWORD 0
global_returnAddress246   QWORD 0
global_hookCallback247    QWORD 0
global_returnAddress247   QWORD 0
global_hookCallback248    QWORD 0
global_returnAddress248   QWORD 0
global_hookCallback249    QWORD 0
global_returnAddress249   QWORD 0
global_hookCallback250    QWORD 0
global_returnAddress250   QWORD 0
global_hookCallback251    QWORD 0
global_returnAddress251   QWORD 0
global_hookCallback252    QWORD 0
global_returnAddress252   QWORD 0
global_hookCallback253    QWORD 0
global_returnAddress253   QWORD 0
global_hookCallback254    QWORD 0
global_returnAddress254   QWORD 0
global_hookCallback255    QWORD 0
global_returnAddress255   QWORD 0

.CODE

HOOK_STUB MACRO stubName:REQ, callbackVar:REQ, returnVar:REQ
    LOCAL noCallback

stubName PROC

    push    r15
    push    r14
    push    r13
    push    r12
    push    r11
    push    r10
    push    r9
    push    r8
    push    rdi
    push    rsi
    push    rbp
    push    rbx
    push    rdx
    push    rcx
    push    rax

    mov     rcx, rsp            ; arg 1: pointer to the saved block
    mov     rbp, rsp

    ; rsp must be 16-byte aligned before the call; the incoming alignment is
    ; not known.
    and     rsp, -16
    sub     rsp, 128            ; 32 shadow space + 96 for xmm0-xmm5

    movups  xmmword ptr [rsp+32], xmm0
    movups  xmmword ptr [rsp+48], xmm1
    movups  xmmword ptr [rsp+64], xmm2
    movups  xmmword ptr [rsp+80], xmm3
    movups  xmmword ptr [rsp+96], xmm4
    movups  xmmword ptr [rsp+112], xmm5

    mov     rax, callbackVar
    test    rax, rax
    jz      noCallback
    call    rax
noCallback:

    movups  xmm0, xmmword ptr [rsp+32]
    movups  xmm1, xmmword ptr [rsp+48]
    movups  xmm2, xmmword ptr [rsp+64]
    movups  xmm3, xmmword ptr [rsp+80]
    movups  xmm4, xmmword ptr [rsp+96]
    movups  xmm5, xmmword ptr [rsp+112]

    mov     rsp, rbp

    pop     rax
    pop     rcx
    pop     rdx
    pop     rbx
    pop     rbp
    pop     rsi
    pop     rdi
    pop     r8
    pop     r9
    pop     r10
    pop     r11
    pop     r12
    pop     r13
    pop     r14
    pop     r15

    ; Absolute jump to the resume buffer, preserving every register and every
    ; flag.
    push    rax
    mov     rax, returnVar
    xchg    qword ptr [rsp], rax
    ret

stubName ENDP

ENDM

HOOK_STUB hookStub00, global_hookCallback00, global_returnAddress00
HOOK_STUB hookStub01, global_hookCallback01, global_returnAddress01
HOOK_STUB hookStub02, global_hookCallback02, global_returnAddress02
HOOK_STUB hookStub03, global_hookCallback03, global_returnAddress03
HOOK_STUB hookStub04, global_hookCallback04, global_returnAddress04
HOOK_STUB hookStub05, global_hookCallback05, global_returnAddress05
HOOK_STUB hookStub06, global_hookCallback06, global_returnAddress06
HOOK_STUB hookStub07, global_hookCallback07, global_returnAddress07
HOOK_STUB hookStub08, global_hookCallback08, global_returnAddress08
HOOK_STUB hookStub09, global_hookCallback09, global_returnAddress09
HOOK_STUB hookStub10, global_hookCallback10, global_returnAddress10
HOOK_STUB hookStub11, global_hookCallback11, global_returnAddress11
HOOK_STUB hookStub12, global_hookCallback12, global_returnAddress12
HOOK_STUB hookStub13, global_hookCallback13, global_returnAddress13
HOOK_STUB hookStub14, global_hookCallback14, global_returnAddress14
HOOK_STUB hookStub15, global_hookCallback15, global_returnAddress15
HOOK_STUB hookStub16, global_hookCallback16, global_returnAddress16
HOOK_STUB hookStub17, global_hookCallback17, global_returnAddress17
HOOK_STUB hookStub18, global_hookCallback18, global_returnAddress18
HOOK_STUB hookStub19, global_hookCallback19, global_returnAddress19
HOOK_STUB hookStub20, global_hookCallback20, global_returnAddress20
HOOK_STUB hookStub21, global_hookCallback21, global_returnAddress21
HOOK_STUB hookStub22, global_hookCallback22, global_returnAddress22
HOOK_STUB hookStub23, global_hookCallback23, global_returnAddress23
HOOK_STUB hookStub24, global_hookCallback24, global_returnAddress24
HOOK_STUB hookStub25, global_hookCallback25, global_returnAddress25
HOOK_STUB hookStub26, global_hookCallback26, global_returnAddress26
HOOK_STUB hookStub27, global_hookCallback27, global_returnAddress27
HOOK_STUB hookStub28, global_hookCallback28, global_returnAddress28
HOOK_STUB hookStub29, global_hookCallback29, global_returnAddress29
HOOK_STUB hookStub30, global_hookCallback30, global_returnAddress30
HOOK_STUB hookStub31, global_hookCallback31, global_returnAddress31
HOOK_STUB hookStub32, global_hookCallback32, global_returnAddress32
HOOK_STUB hookStub33, global_hookCallback33, global_returnAddress33
HOOK_STUB hookStub34, global_hookCallback34, global_returnAddress34
HOOK_STUB hookStub35, global_hookCallback35, global_returnAddress35
HOOK_STUB hookStub36, global_hookCallback36, global_returnAddress36
HOOK_STUB hookStub37, global_hookCallback37, global_returnAddress37
HOOK_STUB hookStub38, global_hookCallback38, global_returnAddress38
HOOK_STUB hookStub39, global_hookCallback39, global_returnAddress39
HOOK_STUB hookStub40, global_hookCallback40, global_returnAddress40
HOOK_STUB hookStub41, global_hookCallback41, global_returnAddress41
HOOK_STUB hookStub42, global_hookCallback42, global_returnAddress42
HOOK_STUB hookStub43, global_hookCallback43, global_returnAddress43
HOOK_STUB hookStub44, global_hookCallback44, global_returnAddress44
HOOK_STUB hookStub45, global_hookCallback45, global_returnAddress45
HOOK_STUB hookStub46, global_hookCallback46, global_returnAddress46
HOOK_STUB hookStub47, global_hookCallback47, global_returnAddress47
HOOK_STUB hookStub48, global_hookCallback48, global_returnAddress48
HOOK_STUB hookStub49, global_hookCallback49, global_returnAddress49
HOOK_STUB hookStub50, global_hookCallback50, global_returnAddress50
HOOK_STUB hookStub51, global_hookCallback51, global_returnAddress51
HOOK_STUB hookStub52, global_hookCallback52, global_returnAddress52
HOOK_STUB hookStub53, global_hookCallback53, global_returnAddress53
HOOK_STUB hookStub54, global_hookCallback54, global_returnAddress54
HOOK_STUB hookStub55, global_hookCallback55, global_returnAddress55
HOOK_STUB hookStub56, global_hookCallback56, global_returnAddress56
HOOK_STUB hookStub57, global_hookCallback57, global_returnAddress57
HOOK_STUB hookStub58, global_hookCallback58, global_returnAddress58
HOOK_STUB hookStub59, global_hookCallback59, global_returnAddress59
HOOK_STUB hookStub60, global_hookCallback60, global_returnAddress60
HOOK_STUB hookStub61, global_hookCallback61, global_returnAddress61
HOOK_STUB hookStub62, global_hookCallback62, global_returnAddress62
HOOK_STUB hookStub63, global_hookCallback63, global_returnAddress63
HOOK_STUB hookStub64, global_hookCallback64, global_returnAddress64
HOOK_STUB hookStub65, global_hookCallback65, global_returnAddress65
HOOK_STUB hookStub66, global_hookCallback66, global_returnAddress66
HOOK_STUB hookStub67, global_hookCallback67, global_returnAddress67
HOOK_STUB hookStub68, global_hookCallback68, global_returnAddress68
HOOK_STUB hookStub69, global_hookCallback69, global_returnAddress69
HOOK_STUB hookStub70, global_hookCallback70, global_returnAddress70
HOOK_STUB hookStub71, global_hookCallback71, global_returnAddress71
HOOK_STUB hookStub72, global_hookCallback72, global_returnAddress72
HOOK_STUB hookStub73, global_hookCallback73, global_returnAddress73
HOOK_STUB hookStub74, global_hookCallback74, global_returnAddress74
HOOK_STUB hookStub75, global_hookCallback75, global_returnAddress75
HOOK_STUB hookStub76, global_hookCallback76, global_returnAddress76
HOOK_STUB hookStub77, global_hookCallback77, global_returnAddress77
HOOK_STUB hookStub78, global_hookCallback78, global_returnAddress78
HOOK_STUB hookStub79, global_hookCallback79, global_returnAddress79
HOOK_STUB hookStub80, global_hookCallback80, global_returnAddress80
HOOK_STUB hookStub81, global_hookCallback81, global_returnAddress81
HOOK_STUB hookStub82, global_hookCallback82, global_returnAddress82
HOOK_STUB hookStub83, global_hookCallback83, global_returnAddress83
HOOK_STUB hookStub84, global_hookCallback84, global_returnAddress84
HOOK_STUB hookStub85, global_hookCallback85, global_returnAddress85
HOOK_STUB hookStub86, global_hookCallback86, global_returnAddress86
HOOK_STUB hookStub87, global_hookCallback87, global_returnAddress87
HOOK_STUB hookStub88, global_hookCallback88, global_returnAddress88
HOOK_STUB hookStub89, global_hookCallback89, global_returnAddress89
HOOK_STUB hookStub90, global_hookCallback90, global_returnAddress90
HOOK_STUB hookStub91, global_hookCallback91, global_returnAddress91
HOOK_STUB hookStub92, global_hookCallback92, global_returnAddress92
HOOK_STUB hookStub93, global_hookCallback93, global_returnAddress93
HOOK_STUB hookStub94, global_hookCallback94, global_returnAddress94
HOOK_STUB hookStub95, global_hookCallback95, global_returnAddress95
HOOK_STUB hookStub96, global_hookCallback96, global_returnAddress96
HOOK_STUB hookStub97, global_hookCallback97, global_returnAddress97
HOOK_STUB hookStub98, global_hookCallback98, global_returnAddress98
HOOK_STUB hookStub99, global_hookCallback99, global_returnAddress99
HOOK_STUB hookStub100, global_hookCallback100, global_returnAddress100
HOOK_STUB hookStub101, global_hookCallback101, global_returnAddress101
HOOK_STUB hookStub102, global_hookCallback102, global_returnAddress102
HOOK_STUB hookStub103, global_hookCallback103, global_returnAddress103
HOOK_STUB hookStub104, global_hookCallback104, global_returnAddress104
HOOK_STUB hookStub105, global_hookCallback105, global_returnAddress105
HOOK_STUB hookStub106, global_hookCallback106, global_returnAddress106
HOOK_STUB hookStub107, global_hookCallback107, global_returnAddress107
HOOK_STUB hookStub108, global_hookCallback108, global_returnAddress108
HOOK_STUB hookStub109, global_hookCallback109, global_returnAddress109
HOOK_STUB hookStub110, global_hookCallback110, global_returnAddress110
HOOK_STUB hookStub111, global_hookCallback111, global_returnAddress111
HOOK_STUB hookStub112, global_hookCallback112, global_returnAddress112
HOOK_STUB hookStub113, global_hookCallback113, global_returnAddress113
HOOK_STUB hookStub114, global_hookCallback114, global_returnAddress114
HOOK_STUB hookStub115, global_hookCallback115, global_returnAddress115
HOOK_STUB hookStub116, global_hookCallback116, global_returnAddress116
HOOK_STUB hookStub117, global_hookCallback117, global_returnAddress117
HOOK_STUB hookStub118, global_hookCallback118, global_returnAddress118
HOOK_STUB hookStub119, global_hookCallback119, global_returnAddress119
HOOK_STUB hookStub120, global_hookCallback120, global_returnAddress120
HOOK_STUB hookStub121, global_hookCallback121, global_returnAddress121
HOOK_STUB hookStub122, global_hookCallback122, global_returnAddress122
HOOK_STUB hookStub123, global_hookCallback123, global_returnAddress123
HOOK_STUB hookStub124, global_hookCallback124, global_returnAddress124
HOOK_STUB hookStub125, global_hookCallback125, global_returnAddress125
HOOK_STUB hookStub126, global_hookCallback126, global_returnAddress126
HOOK_STUB hookStub127, global_hookCallback127, global_returnAddress127
HOOK_STUB hookStub128, global_hookCallback128, global_returnAddress128
HOOK_STUB hookStub129, global_hookCallback129, global_returnAddress129
HOOK_STUB hookStub130, global_hookCallback130, global_returnAddress130
HOOK_STUB hookStub131, global_hookCallback131, global_returnAddress131
HOOK_STUB hookStub132, global_hookCallback132, global_returnAddress132
HOOK_STUB hookStub133, global_hookCallback133, global_returnAddress133
HOOK_STUB hookStub134, global_hookCallback134, global_returnAddress134
HOOK_STUB hookStub135, global_hookCallback135, global_returnAddress135
HOOK_STUB hookStub136, global_hookCallback136, global_returnAddress136
HOOK_STUB hookStub137, global_hookCallback137, global_returnAddress137
HOOK_STUB hookStub138, global_hookCallback138, global_returnAddress138
HOOK_STUB hookStub139, global_hookCallback139, global_returnAddress139
HOOK_STUB hookStub140, global_hookCallback140, global_returnAddress140
HOOK_STUB hookStub141, global_hookCallback141, global_returnAddress141
HOOK_STUB hookStub142, global_hookCallback142, global_returnAddress142
HOOK_STUB hookStub143, global_hookCallback143, global_returnAddress143
HOOK_STUB hookStub144, global_hookCallback144, global_returnAddress144
HOOK_STUB hookStub145, global_hookCallback145, global_returnAddress145
HOOK_STUB hookStub146, global_hookCallback146, global_returnAddress146
HOOK_STUB hookStub147, global_hookCallback147, global_returnAddress147
HOOK_STUB hookStub148, global_hookCallback148, global_returnAddress148
HOOK_STUB hookStub149, global_hookCallback149, global_returnAddress149
HOOK_STUB hookStub150, global_hookCallback150, global_returnAddress150
HOOK_STUB hookStub151, global_hookCallback151, global_returnAddress151
HOOK_STUB hookStub152, global_hookCallback152, global_returnAddress152
HOOK_STUB hookStub153, global_hookCallback153, global_returnAddress153
HOOK_STUB hookStub154, global_hookCallback154, global_returnAddress154
HOOK_STUB hookStub155, global_hookCallback155, global_returnAddress155
HOOK_STUB hookStub156, global_hookCallback156, global_returnAddress156
HOOK_STUB hookStub157, global_hookCallback157, global_returnAddress157
HOOK_STUB hookStub158, global_hookCallback158, global_returnAddress158
HOOK_STUB hookStub159, global_hookCallback159, global_returnAddress159
HOOK_STUB hookStub160, global_hookCallback160, global_returnAddress160
HOOK_STUB hookStub161, global_hookCallback161, global_returnAddress161
HOOK_STUB hookStub162, global_hookCallback162, global_returnAddress162
HOOK_STUB hookStub163, global_hookCallback163, global_returnAddress163
HOOK_STUB hookStub164, global_hookCallback164, global_returnAddress164
HOOK_STUB hookStub165, global_hookCallback165, global_returnAddress165
HOOK_STUB hookStub166, global_hookCallback166, global_returnAddress166
HOOK_STUB hookStub167, global_hookCallback167, global_returnAddress167
HOOK_STUB hookStub168, global_hookCallback168, global_returnAddress168
HOOK_STUB hookStub169, global_hookCallback169, global_returnAddress169
HOOK_STUB hookStub170, global_hookCallback170, global_returnAddress170
HOOK_STUB hookStub171, global_hookCallback171, global_returnAddress171
HOOK_STUB hookStub172, global_hookCallback172, global_returnAddress172
HOOK_STUB hookStub173, global_hookCallback173, global_returnAddress173
HOOK_STUB hookStub174, global_hookCallback174, global_returnAddress174
HOOK_STUB hookStub175, global_hookCallback175, global_returnAddress175
HOOK_STUB hookStub176, global_hookCallback176, global_returnAddress176
HOOK_STUB hookStub177, global_hookCallback177, global_returnAddress177
HOOK_STUB hookStub178, global_hookCallback178, global_returnAddress178
HOOK_STUB hookStub179, global_hookCallback179, global_returnAddress179
HOOK_STUB hookStub180, global_hookCallback180, global_returnAddress180
HOOK_STUB hookStub181, global_hookCallback181, global_returnAddress181
HOOK_STUB hookStub182, global_hookCallback182, global_returnAddress182
HOOK_STUB hookStub183, global_hookCallback183, global_returnAddress183
HOOK_STUB hookStub184, global_hookCallback184, global_returnAddress184
HOOK_STUB hookStub185, global_hookCallback185, global_returnAddress185
HOOK_STUB hookStub186, global_hookCallback186, global_returnAddress186
HOOK_STUB hookStub187, global_hookCallback187, global_returnAddress187
HOOK_STUB hookStub188, global_hookCallback188, global_returnAddress188
HOOK_STUB hookStub189, global_hookCallback189, global_returnAddress189
HOOK_STUB hookStub190, global_hookCallback190, global_returnAddress190
HOOK_STUB hookStub191, global_hookCallback191, global_returnAddress191
HOOK_STUB hookStub192, global_hookCallback192, global_returnAddress192
HOOK_STUB hookStub193, global_hookCallback193, global_returnAddress193
HOOK_STUB hookStub194, global_hookCallback194, global_returnAddress194
HOOK_STUB hookStub195, global_hookCallback195, global_returnAddress195
HOOK_STUB hookStub196, global_hookCallback196, global_returnAddress196
HOOK_STUB hookStub197, global_hookCallback197, global_returnAddress197
HOOK_STUB hookStub198, global_hookCallback198, global_returnAddress198
HOOK_STUB hookStub199, global_hookCallback199, global_returnAddress199
HOOK_STUB hookStub200, global_hookCallback200, global_returnAddress200
HOOK_STUB hookStub201, global_hookCallback201, global_returnAddress201
HOOK_STUB hookStub202, global_hookCallback202, global_returnAddress202
HOOK_STUB hookStub203, global_hookCallback203, global_returnAddress203
HOOK_STUB hookStub204, global_hookCallback204, global_returnAddress204
HOOK_STUB hookStub205, global_hookCallback205, global_returnAddress205
HOOK_STUB hookStub206, global_hookCallback206, global_returnAddress206
HOOK_STUB hookStub207, global_hookCallback207, global_returnAddress207
HOOK_STUB hookStub208, global_hookCallback208, global_returnAddress208
HOOK_STUB hookStub209, global_hookCallback209, global_returnAddress209
HOOK_STUB hookStub210, global_hookCallback210, global_returnAddress210
HOOK_STUB hookStub211, global_hookCallback211, global_returnAddress211
HOOK_STUB hookStub212, global_hookCallback212, global_returnAddress212
HOOK_STUB hookStub213, global_hookCallback213, global_returnAddress213
HOOK_STUB hookStub214, global_hookCallback214, global_returnAddress214
HOOK_STUB hookStub215, global_hookCallback215, global_returnAddress215
HOOK_STUB hookStub216, global_hookCallback216, global_returnAddress216
HOOK_STUB hookStub217, global_hookCallback217, global_returnAddress217
HOOK_STUB hookStub218, global_hookCallback218, global_returnAddress218
HOOK_STUB hookStub219, global_hookCallback219, global_returnAddress219
HOOK_STUB hookStub220, global_hookCallback220, global_returnAddress220
HOOK_STUB hookStub221, global_hookCallback221, global_returnAddress221
HOOK_STUB hookStub222, global_hookCallback222, global_returnAddress222
HOOK_STUB hookStub223, global_hookCallback223, global_returnAddress223
HOOK_STUB hookStub224, global_hookCallback224, global_returnAddress224
HOOK_STUB hookStub225, global_hookCallback225, global_returnAddress225
HOOK_STUB hookStub226, global_hookCallback226, global_returnAddress226
HOOK_STUB hookStub227, global_hookCallback227, global_returnAddress227
HOOK_STUB hookStub228, global_hookCallback228, global_returnAddress228
HOOK_STUB hookStub229, global_hookCallback229, global_returnAddress229
HOOK_STUB hookStub230, global_hookCallback230, global_returnAddress230
HOOK_STUB hookStub231, global_hookCallback231, global_returnAddress231
HOOK_STUB hookStub232, global_hookCallback232, global_returnAddress232
HOOK_STUB hookStub233, global_hookCallback233, global_returnAddress233
HOOK_STUB hookStub234, global_hookCallback234, global_returnAddress234
HOOK_STUB hookStub235, global_hookCallback235, global_returnAddress235
HOOK_STUB hookStub236, global_hookCallback236, global_returnAddress236
HOOK_STUB hookStub237, global_hookCallback237, global_returnAddress237
HOOK_STUB hookStub238, global_hookCallback238, global_returnAddress238
HOOK_STUB hookStub239, global_hookCallback239, global_returnAddress239
HOOK_STUB hookStub240, global_hookCallback240, global_returnAddress240
HOOK_STUB hookStub241, global_hookCallback241, global_returnAddress241
HOOK_STUB hookStub242, global_hookCallback242, global_returnAddress242
HOOK_STUB hookStub243, global_hookCallback243, global_returnAddress243
HOOK_STUB hookStub244, global_hookCallback244, global_returnAddress244
HOOK_STUB hookStub245, global_hookCallback245, global_returnAddress245
HOOK_STUB hookStub246, global_hookCallback246, global_returnAddress246
HOOK_STUB hookStub247, global_hookCallback247, global_returnAddress247
HOOK_STUB hookStub248, global_hookCallback248, global_returnAddress248
HOOK_STUB hookStub249, global_hookCallback249, global_returnAddress249
HOOK_STUB hookStub250, global_hookCallback250, global_returnAddress250
HOOK_STUB hookStub251, global_hookCallback251, global_returnAddress251
HOOK_STUB hookStub252, global_hookCallback252, global_returnAddress252
HOOK_STUB hookStub253, global_hookCallback253, global_returnAddress253
HOOK_STUB hookStub254, global_hookCallback254, global_returnAddress254
HOOK_STUB hookStub255, global_hookCallback255, global_returnAddress255

END
