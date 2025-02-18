/*
 *  Copyright (c) 2022 NetEase Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * Project: CurveCli
 * Created Date: 2022-05-11
 * Author: chengyi (Cyber-SiKu)
 */

package output

import (
	"encoding/json"
	"errors"
	"fmt"

	cmderror "github.com/opencurve/curve/tools-v2/internal/error"
	basecmd "github.com/opencurve/curve/tools-v2/pkg/cli/command"
	"github.com/opencurve/curve/tools-v2/pkg/config"
	"github.com/spf13/viper"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
)

func FinalCmdOutputJson(finalCmd *basecmd.FinalCurveCmd) error {
	output, err := json.MarshalIndent(finalCmd, "", "  ")
	if err != nil {
		return err
	}
	fmt.Println(string(output))
	return nil
}

func FinalCmdOutputPlain(finalCmd *basecmd.FinalCurveCmd,
	funcs basecmd.FinalCurveCmdFunc) error {
	if len(finalCmd.Table.Row) > 0 {
		fmt.Println(finalCmd.Table)
	}
	if finalCmd.Error.Code != cmderror.CODE_SUCCESS {
		return errors.New(finalCmd.Error.Message)
	}
	return nil
}

func FinalCmdOutput(finalCmd *basecmd.FinalCurveCmd,
	funcs basecmd.FinalCurveCmdFunc) error {
	format := finalCmd.Cmd.Flag("format").Value.String()
	finalCmd.Error = cmderror.MostImportantCmdError(cmderror.AllError)
	var err error
	switch format {
	case config.FORMAT_JSON:
		err = FinalCmdOutputJson(finalCmd)
	case config.FORMAT_PLAIN:
		err = funcs.ResultPlainOutput()
	case config.FORMAT_NOOUT:
		err = nil
	default:
		err = fmt.Errorf("the output format %s is not recognized", format)
	}
	if viper.GetBool(config.VIPER_GLOBALE_SHOWERROR) {
		for _, output := range cmderror.AllError {
			fmt.Printf("%+v\n", *output)
		}
	}
	return err
}

func MarshalProtoJson(message proto.Message) (interface{}, error) {
	m := protojson.MarshalOptions{
		Multiline: true,
		Indent:    "  ",
	}
	jsonByte, err := m.Marshal(message)
	if err != nil {
		return nil, err
	}
	var ret interface{}
	err = json.Unmarshal(jsonByte, &ret)
	if err != nil {
		return nil, err
	}
	return ret, nil
}

func SetFinalCmdNoOutput(finalCmd *basecmd.FinalCurveCmd) {
	finalCmd.Cmd.SetArgs([]string{"--format", "noout"})
}
